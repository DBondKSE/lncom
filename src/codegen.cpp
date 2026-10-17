#include "codegen.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <iostream>
#include <map>
#include <memory>
#include <string>

using namespace llvm;

static std::string wider(const std::string &a, const std::string &b) {
  return a == "i64" || b == "i64" ? "i64" : a;
}

class CodeGen : public Visitor {
  LLVMContext ctx;
  IRBuilder<> builder{ctx};
  std::unique_ptr<Module> module;
  Type *i1, *i32, *i64, *ptr;
  Function *printfFn, *dprintfFn, *exitFn;
  Function *scanfFn = nullptr;
  Function *function;
  const FnNode *current_fn = nullptr;
  std::map<std::string, StructType *> struct_types;
  std::map<const FnNode *, Function *> functions;
  std::map<const DeclNode *, Value *> slots;
  std::map<std::string, Value *> texts;
  Value *self = nullptr;
  Value *result = nullptr;

  Type *llvm_type(const std::string &type) {
    if (type.empty())
      return Type::getVoidTy(ctx);
    if (type == "i64")
      return i64;
    if (type == "bool")
      return i1;
    if (type == "i32")
      return i32;
    return struct_types.at(type);
  }

  Value *eval(ExprNode &node) {
    node.accept(*this);
    return result;
  }

  Value *coerce(Value *value, const std::string &have,
                const std::string &want) {
    if (have == "i32" && want == "i64")
      return builder.CreateSExt(value, i64, "wide");
    return value;
  }

  Value *eval_as(ExprNode &node, const std::string &want) {
    return coerce(eval(node), node.type, want);
  }

  Value *entry_alloca(Type *type, const std::string &name) {
    IRBuilderBase::InsertPointGuard guard(builder);
    BasicBlock &entry = function->getEntryBlock();
    builder.SetInsertPoint(&entry, entry.getFirstNonPHIOrDbgOrAlloca());
    return builder.CreateAlloca(type, nullptr, name);
  }

  Value *address(const DeclNode *decl, const std::vector<Link> &links) {
    Value *slot = slots.at(decl);
    if (links.empty())
      return slot;
    std::vector<Value *> indices = {ConstantInt::get(i32, 0)};
    for (const Link &link : links)
      indices.push_back(ConstantInt::get(i32, link.index));
    return builder.CreateInBoundsGEP(llvm_type(decl->type_name), slot, indices,
                                     chain(decl->name, links));
  }

  Value *text(const std::string &s) {
    auto it = texts.find(s);
    if (it != texts.end())
      return it->second;
    return texts[s] = builder.CreateGlobalString(s, ".str");
  }

  void print(const std::string &format, std::vector<Value *> args = {}) {
    args.insert(args.begin(), text(format));
    builder.CreateCall(printfFn, args);
  }

  void print_value(Value *value, const std::string &type,
                   const std::string &prefix, const std::string &suffix) {
    if (type == "bool")
      print(prefix + "%s" + suffix,
            {builder.CreateSelect(value, text("true"), text("false"), "text")});
    else
      print(prefix + (type == "i64" ? "%lld" : "%d") + suffix, {value});
  }

  void print_struct(const StructNode &type, Value *object,
                    const std::string &close) {
    print(type.name + "{");
    for (unsigned i = 0; i < type.fields.size(); i++) {
      const FieldNode &field = *type.fields[i];
      std::string label = (i ? ", " : "") + field.name + ": ";
      Value *at = builder.CreateStructGEP(struct_types.at(type.name), object, i,
                                          field.name);
      if (field.struct_type) {
        print(label);
        print_struct(*field.struct_type, at, "}");
      } else {
        print_value(builder.CreateLoad(llvm_type(field.type_name), at,
                                       field.name),
                    field.type_name, label, "");
      }
    }
    print(close);
  }

  void declare(const FnNode &fn, const std::string &name, bool member) {
    std::vector<Type *> params;
    if (member)
      params.push_back(ptr);
    for (const std::unique_ptr<ParamNode> &param : fn.params)
      params.push_back(llvm_type(param->type_name));
    functions[&fn] = Function::Create(
        FunctionType::get(llvm_type(fn.result_type), params, false),
        Function::ExternalLinkage, name, module.get());
  }

  void declare_global(DeclNode &decl) {
    Type *type = llvm_type(decl.type_name);
    slots[&decl] = new GlobalVariable(*module, type, false,
                                      GlobalValue::InternalLinkage,
                                      Constant::getNullValue(type),
                                      "g_" + decl.name);
  }

  void emit_fn(FnNode &node, const DeclNode *self_decl) {
    IRBuilderBase::InsertPointGuard guard(builder);
    Function *outer = function;
    function = functions.at(&node);
    current_fn = &node;
    builder.SetInsertPoint(BasicBlock::Create(ctx, "entry", function));
    unsigned first = 0;
    if (self_decl) {
      self = function->getArg(0);
      self->setName("this");
      slots[self_decl] = self;
      first = 1;
    }
    for (unsigned i = 0; i < node.params.size(); i++) {
      const ParamNode &param = *node.params[i];
      Argument *arg = function->getArg(first + i);
      arg->setName(param.name);
      Value *slot = entry_alloca(arg->getType(), param.name + ".addr");
      builder.CreateStore(arg, slot);
      slots[&param] = slot;
    }
    node.body->accept(*this);
    if (!builder.GetInsertBlock()->getTerminator())
      builder.CreateRetVoid();
    current_fn = nullptr;
    self = nullptr;
    function = outer;
  }

  Value *object_address(ExprNode &object) {
    if (VarNode *var = dynamic_cast<VarNode *>(&object))
      return address(var->decl, var->links);
    Value *slot = entry_alloca(llvm_type(object.type), "tmp");
    builder.CreateStore(eval(object), slot);
    return slot;
  }

  void emit_block(BlockNode &block, BasicBlock *bb, BasicBlock *next_bb) {
    builder.SetInsertPoint(bb);
    block.accept(*this);
    if (!builder.GetInsertBlock()->getTerminator())
      builder.CreateBr(next_bb);
  }

  Value *checked(Intrinsic::ID id, Value *l, Value *r, const BinOpNode &node,
                 const std::string &type) {
    Value *pair = builder.CreateIntrinsic(id, {l->getType()}, {l, r});
    Value *value = builder.CreateExtractValue(pair, 0, "value");
    Value *overflow = builder.CreateExtractValue(pair, 1, "overflow");
    BasicBlock *fail_bb = BasicBlock::Create(ctx, "overflow", function);
    BasicBlock *ok_bb = BasicBlock::Create(ctx, "ok", function);
    builder.CreateCondBr(overflow, fail_bb, ok_bb);
    builder.SetInsertPoint(fail_bb);
    std::string message = "runtime error: line " + std::to_string(node.line) +
                          ":" + std::to_string(node.col) + ": " + type +
                          " overflow in '" + node.op + "'\n";
    builder.CreateCall(dprintfFn, {ConstantInt::get(i32, 2), text(message)});
    builder.CreateCall(exitFn, {ConstantInt::get(i32, 1)});
    builder.CreateUnreachable();
    builder.SetInsertPoint(ok_bb);
    return value;
  }

public:
  CodeGen() {
    i1 = Type::getInt1Ty(ctx);
    i32 = Type::getInt32Ty(ctx);
    i64 = Type::getInt64Ty(ctx);
    ptr = PointerType::get(ctx, 0);
    module = std::make_unique<Module>("lncom", ctx);
#if LLVM_VERSION_MAJOR >= 21
    module->setTargetTriple(Triple(sys::getDefaultTargetTriple()));
#else
    module->setTargetTriple(sys::getDefaultTargetTriple());
#endif

    function =
        Function::Create(FunctionType::get(i32, false),
                         Function::ExternalLinkage, "main", module.get());
    builder.SetInsertPoint(BasicBlock::Create(ctx, "entry", function));

    printfFn = Function::Create(FunctionType::get(i32, {ptr}, true),
                                Function::ExternalLinkage, "printf",
                                module.get());
    dprintfFn = Function::Create(FunctionType::get(i32, {i32, ptr}, true),
                                 Function::ExternalLinkage, "dprintf",
                                 module.get());
    exitFn = Function::Create(
        FunctionType::get(Type::getVoidTy(ctx), {i32}, false),
        Function::ExternalLinkage, "exit", module.get());
    exitFn->setDoesNotReturn();
  }

  Module &getModule() { return *module; }

  void visit_program(ProgramNode &node) override {
    for (std::unique_ptr<StructNode> &s : node.structs)
      s->accept(*this);
    for (std::unique_ptr<StmtNode> &stmt : node.statements)
      if (DeclNode *decl = dynamic_cast<DeclNode *>(stmt.get()))
        declare_global(*decl);
    for (std::unique_ptr<StructNode> &s : node.structs)
      for (std::unique_ptr<MemberFnNode> &m : s->methods)
        declare(*m, s->name + "." + m->name, true);
    for (std::unique_ptr<FnNode> &fn : node.functions)
      declare(*fn, "fn_" + fn->name, false);
    for (std::unique_ptr<StructNode> &s : node.structs)
      for (std::unique_ptr<MemberFnNode> &m : s->methods)
        m->accept(*this);
    for (std::unique_ptr<FnNode> &fn : node.functions)
      fn->accept(*this);
    for (std::unique_ptr<StmtNode> &stmt : node.statements)
      stmt->accept(*this);
    node.exit->accept(*this);
  }

  void visit_struct(StructNode &node) override {
    std::vector<Type *> fields;
    for (std::unique_ptr<FieldNode> &field : node.fields)
      fields.push_back(llvm_type(field->type_name));
    struct_types[node.name] = StructType::create(ctx, fields, node.name);
  }

  void visit_fn(FnNode &node) override { emit_fn(node, nullptr); }

  void visit_member_fn(MemberFnNode &node) override {
    emit_fn(node, node.self.get());
  }

  void visit_decl(DeclNode &node) override {
    Type *type = llvm_type(node.type_name);
    Value *slot = node.global ? slots.at(&node) : entry_alloca(type, node.name);
    std::vector<std::unique_ptr<ExprNode>> &values = node.init->values;
    if (!node.struct_type || node.init->copy) {
      builder.CreateStore(eval_as(*values[0], node.type_name), slot);
    } else {
      for (unsigned i = 0; i < values.size(); i++) {
        const FieldNode &field = *node.struct_type->fields[i];
        Value *value = eval_as(*values[i], field.type_name);
        builder.CreateStore(
            value, builder.CreateStructGEP(type, slot, i,
                                           node.name + "." + field.name));
      }
    }
    slots[&node] = slot;
  }

  void visit_assign(AssignNode &node) override {
    builder.CreateStore(eval_as(*node.value, node.type),
                        address(node.decl, node.links));
  }

  void visit_call_stmt(CallStmtNode &node) override {
    node.call->accept(*this);
  }

  void visit_exit(ExitNode &node) override {
    if (current_fn) {
      if (current_fn->result_type.empty())
        builder.CreateRetVoid();
      else
        builder.CreateRet(eval_as(*node.value, current_fn->result_type));
      return;
    }
    print_value(eval_as(*node.value, "i64"),
                node.value->type == "bool" ? "bool" : "i64",
                "Program exit with result ", "\n");
    builder.CreateRet(ConstantInt::get(i32, 0));
  }

  void visit_print(PrintNode &node) override {
    if (!node.value) {
      print("%s\n", {text(node.text)});
      return;
    }
    if (!node.struct_type) {
      print_value(eval(*node.value), node.value->type, "", "\n");
      return;
    }
    print_struct(*node.struct_type, object_address(*node.value), "}\n");
  }

  void visit_read(ReadNode &) override {
    if (!scanfFn)
      scanfFn = Function::Create(FunctionType::get(i32, {ptr}, true),
                                 Function::ExternalLinkage, "scanf",
                                 module.get());
    Value *slot = entry_alloca(i32, "read.slot");
    builder.CreateStore(ConstantInt::get(i32, 0), slot);
    builder.CreateCall(scanfFn, {text("%d"), slot});
    result = builder.CreateLoad(i32, slot, "read");
  }

  void visit_block(BlockNode &node) override {
    for (std::unique_ptr<StmtNode> &stmt : node.statements)
      stmt->accept(*this);
    if (node.exit)
      node.exit->accept(*this);
  }

  void visit_if(IfNode &node) override {
    Value *cond = eval(*node.cond);
    BasicBlock *then_bb = BasicBlock::Create(ctx, "then", function);
    BasicBlock *else_bb =
        node.else_block ? BasicBlock::Create(ctx, "else", function) : nullptr;
    BasicBlock *merge_bb = BasicBlock::Create(ctx, "merge", function);
    builder.CreateCondBr(cond, then_bb, else_bb ? else_bb : merge_bb);

    emit_block(*node.then_block, then_bb, merge_bb);
    if (else_bb)
      emit_block(*node.else_block, else_bb, merge_bb);
    builder.SetInsertPoint(merge_bb);
  }

  void visit_while(WhileNode &node) override {
    BasicBlock *cond_bb = BasicBlock::Create(ctx, "cond", function);
    BasicBlock *body_bb = BasicBlock::Create(ctx, "body", function);
    BasicBlock *end_bb = BasicBlock::Create(ctx, "end", function);
    builder.CreateBr(cond_bb);

    builder.SetInsertPoint(cond_bb);
    builder.CreateCondBr(eval(*node.cond), body_bb, end_bb);

    emit_block(*node.body, body_bb, cond_bb);
    builder.SetInsertPoint(end_bb);
  }

  void visit_not(NotNode &node) override {
    result = builder.CreateNot(eval(*node.operand), "not");
  }

  void visit_binop(BinOpNode &node) override {
    std::string type = wider(node.left->type, node.right->type);
    Value *l = eval_as(*node.left, type);
    Value *r = eval_as(*node.right, type);
    if (node.op == "+")
      result = checked(Intrinsic::sadd_with_overflow, l, r, node, type);
    else if (node.op == "-")
      result = checked(Intrinsic::ssub_with_overflow, l, r, node, type);
    else if (node.op == "*")
      result = checked(Intrinsic::smul_with_overflow, l, r, node, type);
    else if (node.op == "==")
      result = builder.CreateICmpEQ(l, r, "eq");
    else
      result = builder.CreateICmpNE(l, r, "ne");
  }

  void visit_var(VarNode &node) override {
    result = builder.CreateLoad(
        llvm_type(node.type), address(node.decl, node.links),
        node.links.empty() ? node.name : node.links.back().name);
  }

  void call(const FnNode &fn, std::vector<Value *> args,
            std::vector<std::unique_ptr<ExprNode>> &values,
            const std::string &name) {
    for (size_t i = 0; i < values.size(); i++)
      args.push_back(eval_as(*values[i], fn.params[i]->type_name));
    result = builder.CreateCall(functions.at(&fn), args,
                                fn.result_type.empty() ? "" : name);
  }

  void visit_call(CallNode &node) override {
    std::vector<Value *> args;
    if (dynamic_cast<const MemberFnNode *>(node.fn))
      args.push_back(self);
    call(*node.fn, args, node.args, node.name);
  }

  void visit_member_call(MemberCallNode &node) override {
    call(*node.fn, {object_address(*node.object)}, node.args, node.name);
  }

  void visit_field_of(FieldOfNode &node) override {
    std::vector<unsigned> indices;
    for (const Link &link : node.links)
      indices.push_back(link.index);
    result = builder.CreateExtractValue(eval(*node.object), indices,
                                        node.links.back().name);
  }

  void visit_init(InitNode &node) override {
    if (node.copy) {
      result = eval(*node.values[0]);
      return;
    }
    Value *object = PoisonValue::get(llvm_type(node.type));
    for (unsigned i = 0; i < node.values.size(); i++) {
      const FieldNode &field = *node.struct_type->fields[i];
      Value *value = eval_as(*node.values[i], field.type_name);
      object = builder.Insert(InsertValueInst::Create(object, value, {i}),
                              node.type + "." + field.name);
    }
    result = object;
  }

  void visit_int(IntNode &node) override {
    result = ConstantInt::get(llvm_type(node.type), node.value, true);
  }

  void visit_bool(BoolNode &node) override {
    result = ConstantInt::get(i1, node.value);
  }
};

int compile(ProgramNode &program, const char *out_path) {
  CodeGen codegen;
  program.accept(codegen);
  if (verifyModule(codegen.getModule(), &errs()))
    return 1;

  std::error_code ec;
  raw_fd_ostream out(out_path, ec);
  if (ec) {
    std::cerr << "error: cannot write " << out_path << "\n";
    return 1;
  }
  codegen.getModule().print(out, nullptr);
  return 0;
}
