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
  std::map<const MutexNode *, Value *> mutexes;
  std::vector<Value *> held;
  std::map<const FnNode *, StructType *> args_types;
  std::map<const FnNode *, Function *> trampolines;
  Value *self = nullptr;
  Value *result = nullptr;

  struct Started {
    const CallNode *call;
    Value *args, *tid;
    DeclNode *decl;
    Value *count;
  };

  Type *llvm_type(const std::string &type) {
    if (type.empty())
      return Type::getVoidTy(ctx);
    if (is_array_type(type))
      return ArrayType::get(llvm_type(element_type(type)), array_size(type));
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

  Type *param_type(const std::string &type) {
    return is_array_type(type) ? ptr : llvm_type(type);
  }

  FunctionCallee libc(const std::string &name, Type *result,
                      std::vector<Type *> params) {
    return module->getOrInsertFunction(
        name, FunctionType::get(result, params, false));
  }

  Value *size_of(const std::string &type) {
    return ConstantExpr::getSizeOf(llvm_type(type));
  }

  void copy(Value *dest, Value *src, const std::string &type) {
    builder.CreateMemMove(dest, MaybeAlign(), src, MaybeAlign(),
                          size_of(type));
  }

  template <class Body>
  void loop(Value *from, Value *to, const std::string &name, Body body) {
    Value *slot = entry_alloca(i64, name + ".i");
    builder.CreateStore(from, slot);
    BasicBlock *cond_bb = BasicBlock::Create(ctx, name + ".cond", function);
    BasicBlock *body_bb = BasicBlock::Create(ctx, name + ".body", function);
    BasicBlock *end_bb = BasicBlock::Create(ctx, name + ".end", function);
    builder.CreateBr(cond_bb);
    builder.SetInsertPoint(cond_bb);
    Value *i = builder.CreateLoad(i64, slot, "i");
    builder.CreateCondBr(builder.CreateICmpSLT(i, to, "more"), body_bb, end_bb);
    builder.SetInsertPoint(body_bb);
    body(i);
    builder.CreateStore(builder.CreateAdd(i, ConstantInt::get(i64, 1), "next"),
                        slot);
    builder.CreateBr(cond_bb);
    builder.SetInsertPoint(end_bb);
  }

  void fail_if(Value *cond, const std::string &format,
               std::vector<Value *> args = {}) {
    BasicBlock *fail_bb = BasicBlock::Create(ctx, "fail", function);
    BasicBlock *ok_bb = BasicBlock::Create(ctx, "ok", function);
    builder.CreateCondBr(cond, fail_bb, ok_bb);
    builder.SetInsertPoint(fail_bb);
    args.insert(args.begin(), {ConstantInt::get(i32, 2), text(format)});
    builder.CreateCall(dprintfFn, args);
    builder.CreateCall(exitFn, {ConstantInt::get(i32, 1)});
    builder.CreateUnreachable();
    builder.SetInsertPoint(ok_bb);
  }

  static std::string at(const ASTNode &node) {
    return "runtime error: line " + std::to_string(node.line) + ":" +
           std::to_string(node.col) + ": ";
  }

  void fill(Value *dest, ArrayNode &node) {
    std::string type = array_type(node.element, node.size);
    if (!node.fill) {
      builder.CreateMemSet(dest, ConstantInt::get(Type::getInt8Ty(ctx), 0),
                           size_of(type), MaybeAlign());
      return;
    }
    Value *value = eval_as(*node.fill, node.element);
    loop(ConstantInt::get(i64, 0), ConstantInt::get(i64, node.size), "fill",
         [&](Value *i) {
           builder.CreateStore(
               value, builder.CreateInBoundsGEP(llvm_type(type), dest,
                                                {ConstantInt::get(i64, 0), i},
                                                "cell"));
         });
  }

  Value *private_copy(ExprNode &value) {
    if (!dynamic_cast<VarNode *>(&value))
      return object_address(value);
    Value *slot = entry_alloca(llvm_type(value.type), "copy");
    copy(slot, object_address(value), value.type);
    return slot;
  }

  void copy_into(Value *dest, ExprNode &value, const std::string &type) {
    if (!is_array_type(type)) {
      builder.CreateStore(eval_as(value, type), dest);
      return;
    }
    if (ArrayNode *array = dynamic_cast<ArrayNode *>(&value))
      fill(dest, *array);
    else
      copy(dest, object_address(value), type);
  }

  Value *element_address(Value *base, const std::string &type,
                         ExprNode &index, const ASTNode &at_node) {
    Value *i = eval_as(index, "i64");
    uint64_t size = array_size(type);
    if (!dynamic_cast<IntNode *>(&index))
      fail_if(builder.CreateICmpUGE(i, ConstantInt::get(i64, size), "outside"),
              at(at_node) + "index %lld is out of range for an array of " +
                  std::to_string(size) + "\n",
              {i});
    return builder.CreateInBoundsGEP(llvm_type(type), base,
                                     {ConstantInt::get(i64, 0), i}, "cell");
  }

  void print_array(Value *array, const std::string &type,
                   const std::string &close) {
    std::string element = element_type(type);
    Type *array_ty = llvm_type(type);
    auto cell = [&](Value *i) {
      return builder.CreateLoad(
          llvm_type(element),
          builder.CreateInBoundsGEP(array_ty, array,
                                    {ConstantInt::get(i64, 0), i}, "cell"),
          "value");
    };
    print_value(cell(ConstantInt::get(i64, 0)), element, "[", "");
    loop(ConstantInt::get(i64, 1), ConstantInt::get(i64, array_size(type)),
         "print", [&](Value *i) { print_value(cell(i), element, ", ", ""); });
    print("]" + close);
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
      } else if (is_array_type(field.type_name)) {
        print(label);
        print_array(at, field.type_name, "");
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
      params.push_back(param_type(param->type_name));
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
    std::vector<Value *> outer_held;
    outer_held.swap(held);
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
      if (is_array_type(param.type_name)) {
        slots[&param] = arg;
        continue;
      }
      Value *slot = entry_alloca(arg->getType(), param.name + ".addr");
      builder.CreateStore(arg, slot);
      slots[&param] = slot;
    }
    node.body->accept(*this);
    if (!builder.GetInsertBlock()->getTerminator())
      builder.CreateRetVoid();
    held.swap(outer_held);
    current_fn = nullptr;
    self = nullptr;
    function = outer;
  }

  Value *object_address(ExprNode &object) {
    if (VarNode *var = dynamic_cast<VarNode *>(&object))
      return address(var->decl, var->links);
    if (ArrayNode *array = dynamic_cast<ArrayNode *>(&object)) {
      Value *slot = entry_alloca(llvm_type(object.type), "array");
      fill(slot, *array);
      return slot;
    }
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
    fail_if(overflow, at(node) + type + " overflow in '" + node.op + "'\n");
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
        if (decl->global)
          declare_global(*decl);
    for (std::unique_ptr<MutexNode> &m : node.mutexes) {
      Type *type = ArrayType::get(Type::getInt8Ty(ctx), 64);
      GlobalVariable *slot = new GlobalVariable(
          *module, type, false, GlobalValue::InternalLinkage,
          Constant::getNullValue(type), "mutex_" + m->name);
      slot->setAlignment(Align(16));
      mutexes[m.get()] = slot;
      builder.CreateCall(libc("pthread_mutex_init", i32, {ptr, ptr}),
                         {slot, ConstantPointerNull::get(
                                    PointerType::get(ctx, 0))});
    }
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
      copy_into(slot, *values[0], node.type_name);
    } else {
      for (unsigned i = 0; i < values.size(); i++) {
        const FieldNode &field = *node.struct_type->fields[i];
        copy_into(builder.CreateStructGEP(type, slot, i,
                                          node.name + "." + field.name),
                  *values[i], field.type_name);
      }
    }
    slots[&node] = slot;
  }

  void visit_assign(AssignNode &node) override {
    Value *target = address(node.decl, node.links);
    if (node.index) {
      const std::string &array =
          node.links.empty()
              ? node.decl->type_name
              : field_type(*node.decl, node.links);
      target = element_address(target, array, *node.index, *node.index);
    }
    copy_into(target, *node.value, node.type);
  }

  std::string field_type(const DeclNode &decl, const std::vector<Link> &links) {
    const StructNode *owner = decl.struct_type;
    const FieldNode *field = nullptr;
    for (const Link &link : links) {
      field = owner->fields[link.index].get();
      owner = field->struct_type;
    }
    return field->type_name;
  }

  void visit_call_stmt(CallStmtNode &node) override {
    node.call->accept(*this);
  }

  void unlock_all() {
    for (auto it = held.rbegin(); it != held.rend(); ++it)
      builder.CreateCall(libc("pthread_mutex_unlock", i32, {ptr}), {*it});
  }

  void visit_exit(ExitNode &node) override {
    if (current_fn) {
      if (current_fn->result_type.empty()) {
        unlock_all();
        builder.CreateRetVoid();
      } else {
        Value *value = eval_as(*node.value, current_fn->result_type);
        unlock_all();
        builder.CreateRet(value);
      }
      return;
    }
    Value *value = eval_as(*node.value, "i64");
    unlock_all();
    print_value(value, node.value->type == "bool" ? "bool" : "i64",
                "Program exit with result ", "\n");
    builder.CreateRet(ConstantInt::get(i32, 0));
  }

  void visit_print(PrintNode &node) override {
    if (!node.value) {
      print("%s\n", {text(node.text)});
      return;
    }
    if (is_array_type(node.value->type)) {
      print_array(object_address(*node.value), node.value->type, "\n");
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
      args.push_back(is_array_type(fn.params[i]->type_name)
                         ? private_copy(*values[i])
                         : eval_as(*values[i], fn.params[i]->type_name));
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

  void visit_array(ArrayNode &node) override {
    result = builder.CreateLoad(llvm_type(node.type), object_address(node),
                                "array");
  }

  void visit_index(IndexNode &node) override {
    Value *cell = element_address(object_address(*node.array),
                                  node.array->type, *node.index, node);
    result = builder.CreateLoad(llvm_type(node.type), cell, "value");
  }

  void visit_thread_index(ThreadIndexNode &) override {}

  void visit_range(RangeNode &) override {}

  void visit_lock(LockNode &node) override {
    Value *mutex = mutexes.at(node.mutex);
    builder.CreateCall(libc("pthread_mutex_lock", i32, {ptr}), {mutex});
    held.push_back(mutex);
    node.body->accept(*this);
    held.pop_back();
    if (!builder.GetInsertBlock()->getTerminator())
      builder.CreateCall(libc("pthread_mutex_unlock", i32, {ptr}), {mutex});
  }

  StructType *args_type(const FnNode &fn) {
    auto it = args_types.find(&fn);
    if (it != args_types.end())
      return it->second;
    std::vector<Type *> fields;
    for (const std::unique_ptr<ParamNode> &param : fn.params)
      fields.push_back(llvm_type(param->type_name));
    if (!fn.result_type.empty())
      fields.push_back(llvm_type(fn.result_type));
    return args_types[&fn] = StructType::create(ctx, fields, fn.name + ".args");
  }

  Function *trampoline(const FnNode &fn) {
    auto it = trampolines.find(&fn);
    if (it != trampolines.end())
      return it->second;
    IRBuilderBase::InsertPointGuard guard(builder);
    Function *outer = function;
    function = Function::Create(FunctionType::get(ptr, {ptr}, false),
                                Function::InternalLinkage, fn.name + ".thread",
                                module.get());
    builder.SetInsertPoint(BasicBlock::Create(ctx, "entry", function));
    Value *args = function->getArg(0);
    args->setName("args");
    StructType *type = args_type(fn);
    std::vector<Value *> values;
    for (unsigned i = 0; i < fn.params.size(); i++) {
      const ParamNode &param = *fn.params[i];
      Value *field = builder.CreateStructGEP(type, args, i, param.name);
      values.push_back(is_array_type(param.type_name)
                           ? field
                           : builder.CreateLoad(llvm_type(param.type_name),
                                                field, param.name));
    }
    Value *value = builder.CreateCall(functions.at(&fn), values,
                                      fn.result_type.empty() ? "" : "result");
    if (!fn.result_type.empty())
      builder.CreateStore(
          value, builder.CreateStructGEP(type, args, fn.params.size(), "result"));
    builder.CreateRet(ConstantPointerNull::get(PointerType::get(ctx, 0)));
    Function *created = function;
    function = outer;
    return trampolines[&fn] = created;
  }

  void store_arg(Value *field, ExprNode &arg, const std::string &type) {
    if (is_array_type(type))
      copy(field, object_address(arg), type);
    else
      builder.CreateStore(eval_as(arg, type), field);
  }

  void create(const CallNode &call, Value *tid, Value *args) {
    Value *rc = builder.CreateCall(
        libc("pthread_create", i32, {ptr, ptr, ptr, ptr}),
        {tid, ConstantPointerNull::get(PointerType::get(ctx, 0)),
         trampoline(*call.fn), args},
        "rc");
    fail_if(builder.CreateICmpNE(rc, ConstantInt::get(i32, 0), "failed"),
            at(call) + "cannot start a thread for '" + call.name + "'\n");
  }

  Started start(CallNode &call, DeclNode *decl) {
    const FnNode &fn = *call.fn;
    StructType *type = args_type(fn);
    Value *args = entry_alloca(type, fn.name + ".args");
    for (unsigned i = 0; i < fn.params.size(); i++)
      store_arg(builder.CreateStructGEP(type, args, i, fn.params[i]->name),
                *call.args[i], fn.params[i]->type_name);
    Value *tid = entry_alloca(ptr, fn.name + ".tid");
    create(call, tid, args);
    return {&call, args, tid, decl, nullptr};
  }

  Started start_range(RangeNode &range) {
    CallNode &call = *range.call;
    const FnNode &fn = *call.fn;
    StructType *type = args_type(fn);
    Value *count = eval_as(*range.count, "i64");
    fail_if(builder.CreateICmpSGT(count, ConstantInt::get(i64, 1024), "many"),
            at(range) + "a range line starts at most 1024 threads, got %lld\n",
            {count});
    std::vector<Value *> shared(fn.params.size(), nullptr);
    for (unsigned i = 0; i < fn.params.size(); i++) {
      if (dynamic_cast<ThreadIndexNode *>(call.args[i].get()))
        continue;
      const std::string &param = fn.params[i]->type_name;
      if (is_array_type(param)) {
        shared[i] = entry_alloca(llvm_type(param), fn.params[i]->name);
        copy(shared[i], object_address(*call.args[i]), param);
      } else {
        shared[i] = eval_as(*call.args[i], param);
      }
    }
    FunctionCallee malloc_fn = libc("malloc", ptr, {i64});
    Value *args = builder.CreateCall(
        malloc_fn,
        {builder.CreateMul(count, ConstantExpr::getSizeOf(type), "bytes")},
        fn.name + ".args");
    Value *tids = builder.CreateCall(
        malloc_fn,
        {builder.CreateMul(count, ConstantExpr::getSizeOf(ptr), "bytes")},
        fn.name + ".tids");
    loop(ConstantInt::get(i64, 0), count, "start", [&](Value *k) {
      Value *slot = builder.CreateInBoundsGEP(type, args, {k}, "slot");
      for (unsigned i = 0; i < fn.params.size(); i++) {
        const std::string &param = fn.params[i]->type_name;
        Value *field =
            builder.CreateStructGEP(type, slot, i, fn.params[i]->name);
        if (!shared[i])
          builder.CreateStore(
              coerce(builder.CreateTrunc(k, i32, "index"), "i32", param),
              field);
        else if (is_array_type(param))
          copy(field, shared[i], param);
        else
          builder.CreateStore(shared[i], field);
      }
      create(call, builder.CreateInBoundsGEP(ptr, tids, {k}, "tid"), slot);
    });
    return {&call, args, tids, nullptr, count};
  }

  void join(Started &started) {
    FunctionCallee join_fn = libc("pthread_join", i32, {ptr, ptr});
    Value *null = ConstantPointerNull::get(PointerType::get(ctx, 0));
    if (started.count) {
      loop(ConstantInt::get(i64, 0), started.count, "join", [&](Value *k) {
        Value *tid = builder.CreateLoad(
            ptr, builder.CreateInBoundsGEP(ptr, started.tid, {k}, "tid"),
            "thread");
        builder.CreateCall(join_fn, {tid, null});
      });
      FunctionCallee free_fn =
          libc("free", Type::getVoidTy(ctx), {ptr});
      builder.CreateCall(free_fn, {started.tid});
      builder.CreateCall(free_fn, {started.args});
      return;
    }
    builder.CreateCall(join_fn,
                       {builder.CreateLoad(ptr, started.tid, "thread"), null});
    DeclNode *decl = started.decl;
    if (!decl)
      return;
    const FnNode &fn = *started.call->fn;
    Value *field = builder.CreateStructGEP(args_type(fn), started.args,
                                           fn.params.size(), "result");
    Value *slot = entry_alloca(llvm_type(decl->type_name), decl->name);
    if (is_array_type(decl->type_name))
      copy(slot, field, decl->type_name);
    else
      builder.CreateStore(
          coerce(builder.CreateLoad(llvm_type(fn.result_type), field,
                                    decl->name),
                 fn.result_type, decl->type_name),
          slot);
    slots[decl] = slot;
  }

  void visit_spawn(SpawnNode &node) override {
    std::vector<Started> started;
    for (std::unique_ptr<StmtNode> &line : node.lines) {
      if (DeclNode *decl = dynamic_cast<DeclNode *>(line.get()))
        started.push_back(
            start(static_cast<CallNode &>(*decl->init->values[0]), decl));
      else if (CallStmtNode *stmt = dynamic_cast<CallStmtNode *>(line.get()))
        started.push_back(start(static_cast<CallNode &>(*stmt->call), nullptr));
      else
        started.push_back(start_range(static_cast<RangeNode &>(*line)));
    }
    for (Started &s : started)
      join(s);
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
