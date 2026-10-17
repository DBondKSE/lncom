#include "semantic.h"
#include "error.h"

static bool is_integer(const std::string &type) {
  return type == "i32" || type == "i64";
}

static bool is_builtin(const std::string &type) {
  return is_integer(type) || type == "bool";
}

static InitNode *as_init(ExprNode &node) {
  return dynamic_cast<InitNode *>(&node);
}

static std::string where(const ASTNode &node) {
  return std::to_string(node.line) + ":" + std::to_string(node.col);
}

static void check_count(const InitNode &init, size_t want) {
  if (init.values.size() != want)
    error(init.line, init.col,
          "wrong number of values in {} for " + init.type + ": expected " +
              std::to_string(want) + ", got " +
              std::to_string(init.values.size()));
}

static std::string const_field(const StructNode &type) {
  for (const std::unique_ptr<FieldNode> &field : type.fields) {
    if (!field->mut)
      return field->name;
    if (field->struct_type) {
      std::string inner = const_field(*field->struct_type);
      if (!inner.empty())
        return field->name + "." + inner;
    }
  }
  return "";
}

static std::string show(const ExprNode &expr) {
  if (const VarNode *v = dynamic_cast<const VarNode *>(&expr))
    return chain(v->name, v->links);
  if (const CallNode *c = dynamic_cast<const CallNode *>(&expr))
    return c->name + (c->args.empty() ? "()" : "(...)");
  if (const MemberCallNode *m = dynamic_cast<const MemberCallNode *>(&expr))
    return show(*m->object) + "." + m->name +
           (m->args.empty() ? "()" : "(...)");
  const FieldOfNode &f = dynamic_cast<const FieldOfNode &>(expr);
  return chain(show(*f.object), f.links);
}

static std::string called_name(const ExprNode &expr) {
  if (const CallNode *c = dynamic_cast<const CallNode *>(&expr))
    return c->name;
  return dynamic_cast<const MemberCallNode &>(expr).name;
}

const std::string &SemanticChecker::check(ExprNode &node) {
  node.accept(*this);
  if (node.type.empty())
    error(node.line, node.col,
          "'" + called_name(node) + "' returns nothing, its call has no value");
  return node.type;
}

const DeclNode *SemanticChecker::find(const std::string &name) const {
  for (auto frame = scopes.rbegin(); frame != scopes.rend(); ++frame) {
    auto it = frame->find(name);
    if (it != frame->end())
      return it->second;
  }
  return nullptr;
}

const DeclNode &SemanticChecker::lookup(const std::string &name,
                                        const ASTNode &at) const {
  if (const DeclNode *decl = find(name))
    return *decl;
  if (functions.count(name))
    error(at.line, at.col, "'" + name + "' is a function, not a variable");
  if (globals.count(name))
    error(at.line, at.col,
          "variable '" + name + "' is used before its declaration");
  error(at.line, at.col, "variable '" + name + "' is not declared");
}

static const MemberFnNode *method(const StructNode &type,
                                  const std::string &name) {
  for (const std::unique_ptr<MemberFnNode> &m : type.methods)
    if (m->name == name)
      return m.get();
  return nullptr;
}

bool SemanticChecker::is_self(const DeclNode &decl) const {
  return current_member && &decl == current_member->self.get();
}

bool SemanticChecker::is_own_field(const std::string &name) const {
  if (!current_member)
    return false;
  for (const std::unique_ptr<FieldNode> &field : current_member->owner->fields)
    if (field->name == name)
      return true;
  return false;
}

const DeclNode &SemanticChecker::root(const std::string &name,
                                      std::vector<Link> &links,
                                      const ASTNode &at) {
  if (const DeclNode *decl = find(name))
    return *decl;
  if (is_own_field(name)) {
    links.insert(links.begin(), Link{name, at.col});
    return *current_member->self;
  }
  if (!current_fn)
    return lookup(name, at);
  auto global = globals.find(name);
  if (global != globals.end()) {
    uses[current_fn].insert(global->second);
    return *global->second;
  }
  if (functions.count(name))
    error(at.line, at.col, "'" + name + "' is a function, not a variable");
  error(at.line, at.col,
        "'" + name + "' is not a parameter, a local variable" +
            (current_member ? ", a field of struct '" +
                                  current_member->owner->name + "'"
                            : "") +
            " or a global variable");
}

std::string SemanticChecker::display(const DeclNode &decl,
                                     const std::vector<Link> &links) const {
  if (!is_self(decl))
    return chain(decl.name, links);
  return chain(links.front().name,
               std::vector<Link>(links.begin() + 1, links.end()));
}

std::string SemanticChecker::const_link(const VarNode &object) const {
  const DeclNode &decl = *object.decl;
  if (dynamic_cast<const ParamNode *>(&decl))
    return "'" + decl.name + "' is a parameter";
  if (!decl.mut)
    return "'" + decl.name + "' is not mut";
  const StructNode *owner = decl.struct_type;
  for (const Link &link : object.links) {
    const FieldNode &field = *owner->fields[link.index];
    if (!field.mut)
      return "field '" + link.name + "' is not mut";
    owner = field.struct_type;
  }
  return "";
}

void SemanticChecker::check_mutation(const MemberCallNode &call,
                                     const std::string &target,
                                     const std::string &blocker) const {
  if (call.fn->mutating)
    error(call.line, call.col,
          "cannot call '" + call.name + "' on '" + target +
              "': it changes the object and " + blocker);
}

void SemanticChecker::mark_mutating(ProgramNode &program) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::unique_ptr<StructNode> &s : program.structs)
      for (std::unique_ptr<MemberFnNode> &m : s->methods)
        for (const MemberFnNode *callee : m->self_calls)
          if (!m->mutating && callee->mutating) {
            m->mutating = true;
            changed = true;
          }
  }
}

void SemanticChecker::collect_globals(ProgramNode &program) {
  for (std::unique_ptr<StmtNode> &stmt : program.statements) {
    DeclNode *decl = dynamic_cast<DeclNode *>(stmt.get());
    if (!decl)
      continue;
    decl->global = true;
    decl->struct_type = resolve_type(decl->type_name, decl->is_struct,
                                     decl->line, decl->type_col);
    global_order.push_back(decl);
    globals.emplace(decl->name, decl);
  }
}

void SemanticChecker::spread_uses() {
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto &[fn, callees] : calls)
      for (const FnNode *callee : callees)
        for (const DeclNode *global : uses[callee])
          if (uses[fn].insert(global).second)
            changed = true;
  }
}

void SemanticChecker::note_call(const FnNode &fn, const std::string &name,
                                const ASTNode &at) {
  if (current_fn) {
    calls[current_fn].insert(&fn);
    return;
  }
  const std::set<const DeclNode *> &used = uses[&fn];
  for (const DeclNode *global : global_order)
    if (used.count(global) && !declared.count(global))
      error(at.line, at.col,
            "call to '" + name + "' uses global '" + global->name +
                "' before its declaration at line " + where(*global));
}

const StructNode *SemanticChecker::resolve_type(const std::string &type,
                                                bool is_struct, uint64_t line,
                                                uint64_t col) const {
  if (!is_struct)
    return nullptr;
  auto it = structs.find(type);
  if (it == structs.end())
    error(line, col, "unknown struct '" + type + "'");
  return it->second;
}

const FieldNode *SemanticChecker::resolve_links(const DeclNode &decl,
                                                std::vector<Link> &links,
                                                uint64_t line,
                                                bool write) const {
  return walk_links(is_self(decl) ? "" : decl.name, decl.type_name,
                    decl.struct_type, links, line, write ? &decl : nullptr);
}

const FieldNode *SemanticChecker::walk_links(std::string path,
                                             const std::string &type_name,
                                             const StructNode *owner,
                                             std::vector<Link> &links,
                                             uint64_t line,
                                             const DeclNode *written) const {
  const std::string *type = &type_name;
  const FieldNode *field = nullptr;
  for (Link &link : links) {
    if (!owner)
      error(line, link.col,
            "'" + path + "' of type " + *type + " has no fields");
    field = nullptr;
    for (unsigned i = 0; i < owner->fields.size() && !field; i++) {
      if (owner->fields[i]->name == link.name) {
        field = owner->fields[i].get();
        link.index = i;
      }
    }
    if (!field)
      error(line, link.col,
            "struct " + *type + " has no field '" + link.name + "'");
    if (written && !field->mut)
      error(line, link.col,
            "cannot assign to '" + display(*written, links) + "': field '" +
                link.name + "' is not mut");
    path += (path.empty() ? "" : ".") + link.name;
    type = &field->type_name;
    owner = field->struct_type;
  }
  return field;
}

void SemanticChecker::check_assignable(const ExprNode &expr,
                                       const std::string &want,
                                       const ASTNode &at,
                                       const std::string &what) const {
  const std::string &have = expr.type;
  if (have == want || (have == "i32" && want == "i64"))
    return;
  const IntNode *literal = dynamic_cast<const IntNode *>(&expr);
  if (literal && is_integer(want))
    error(literal->line, literal->col,
          "constant " + literal->text + " does not fit in " + want);
  error(at.line, at.col,
        "cannot " + what + " of type " + want + " with a value of type " +
            have);
}

void SemanticChecker::visit_program(ProgramNode &node) {
  for (std::unique_ptr<StructNode> &s : node.structs)
    s->accept(*this);
  for (std::unique_ptr<StructNode> &s : node.structs)
    for (std::unique_ptr<MemberFnNode> &m : s->methods)
      declare_params(*m);
  for (std::unique_ptr<FnNode> &fn : node.functions)
    declare_fn(*fn);
  collect_globals(node);
  for (std::unique_ptr<StructNode> &s : node.structs)
    for (std::unique_ptr<MemberFnNode> &m : s->methods)
      m->accept(*this);
  for (std::unique_ptr<FnNode> &fn : node.functions)
    fn->accept(*this);
  mark_mutating(node);
  for (const PendingCall &p : pending)
    check_mutation(*p.call, p.target, p.blocker);
  mutation_known = true;
  spread_uses();
  scopes.push_back({});
  for (std::unique_ptr<StmtNode> &stmt : node.statements)
    stmt->accept(*this);
  node.exit->accept(*this);
  scopes.pop_back();
}

void SemanticChecker::visit_struct(StructNode &node) {
  if (is_builtin(node.name))
    error(node.line, node.col,
          "a struct cannot be named '" + node.name +
              "', it is a built-in type");
  if (structs.count(node.name))
    error(node.line, node.col,
          "struct '" + node.name + "' is already declared");
  std::map<std::string, const FieldNode *> seen;
  for (std::unique_ptr<FieldNode> &field : node.fields) {
    if (field->is_struct && field->type_name == node.name)
      error(field->line, field->type_col,
            "struct '" + node.name + "' cannot contain itself");
    field->struct_type = resolve_type(field->type_name, field->is_struct,
                                      field->line, field->type_col);
    if (!seen.emplace(field->name, field.get()).second)
      error(field->line, field->col,
            "field '" + field->name + "' is already declared in struct '" +
                node.name + "'");
  }
  std::map<std::string, const MemberFnNode *> methods;
  for (std::unique_ptr<MemberFnNode> &method : node.methods) {
    if (seen.count(method->name))
      error(method->line, method->col,
            "member function '" + method->name +
                "' has the same name as a field of struct '" + node.name +
                "'");
    if (!methods.emplace(method->name, method.get()).second)
      error(method->line, method->col,
            "member function '" + method->name +
                "' is already declared in struct '" + node.name + "'");
  }
  structs[node.name] = &node;
}

void SemanticChecker::declare_fn(FnNode &node) {
  if (functions.count(node.name))
    error(node.line, node.col,
          "function '" + node.name + "' is already declared");
  declare_params(node);
  functions[node.name] = &node;
}

void SemanticChecker::declare_params(FnNode &node) {
  std::map<std::string, const ParamNode *> seen;
  for (std::unique_ptr<ParamNode> &param : node.params) {
    param->struct_type = resolve_type(param->type_name, param->is_struct,
                                      param->line, param->type_col);
    if (!seen.emplace(param->name, param.get()).second)
      error(param->line, param->col,
            "parameter '" + param->name + "' is already declared");
  }
  resolve_type(node.result_type, node.result_struct, node.line,
               node.result_col);
}

void SemanticChecker::visit_fn(FnNode &node) { check_body(node); }

void SemanticChecker::visit_member_fn(MemberFnNode &node) {
  current_member = &node;
  check_body(node);
  current_member = nullptr;
}

void SemanticChecker::check_body(FnNode &node) {
  current_fn = &node;
  scopes.push_back({});
  for (std::unique_ptr<ParamNode> &param : node.params)
    scopes.back()[param->name] = param.get();
  for (std::unique_ptr<StmtNode> &stmt : node.body->statements)
    stmt->accept(*this);
  if (node.body->exit)
    node.body->exit->accept(*this);
  scopes.pop_back();
  current_fn = nullptr;
}

void SemanticChecker::visit_decl(DeclNode &node) {
  node.struct_type = resolve_type(node.type_name, node.is_struct, node.line,
                                  node.type_col);
  if (scopes.back().count(node.name))
    error(node.line, node.col,
          "variable '" + node.name + "' is already declared" +
              (scopes.size() > 1 ? " in this block" : ""));
  InitNode &init = *node.init;
  init.type = node.type_name;
  init.struct_type = node.struct_type;
  if (node.struct_type) {
    check(init);
  } else {
    check_count(init, 1);
    ExprNode &value = *init.values[0];
    if (!as_init(value))
      check(value);
    check_value(value, node.type_name, nullptr, node, "'" + node.name + "'");
  }
  scopes.back()[node.name] = &node;
  if (!current_fn && scopes.size() == 1)
    declared.insert(&node);
}

void SemanticChecker::check_value(ExprNode &value, const std::string &type,
                                  const StructNode *struct_type,
                                  const ASTNode &at,
                                  const std::string &target) {
  InitNode *init = as_init(value);
  if (!init) {
    check_assignable(value, type, at, "initialise " + target);
    return;
  }
  if (!struct_type)
    error(init->line, init->col,
          "cannot initialise " + target + " of type " + type + " with {}");
  init->type = type;
  init->struct_type = struct_type;
  check(*init);
}

void SemanticChecker::visit_init(InitNode &node) {
  const std::vector<std::unique_ptr<FieldNode>> &fields =
      node.struct_type->fields;
  for (std::unique_ptr<ExprNode> &value : node.values)
    if (!as_init(*value))
      check(*value);
  node.copy = node.values.size() == 1 && node.values[0]->type == node.type;
  if (node.copy)
    return;
  check_count(node, fields.size());
  for (size_t i = 0; i < fields.size(); i++)
    check_value(*node.values[i], fields[i]->type_name, fields[i]->struct_type,
                *node.values[i], "field '" + fields[i]->name + "'");
}

void SemanticChecker::visit_assign(AssignNode &node) {
  std::string target = chain(node.name, node.links);
  std::string object = node.links.empty() ? "it" : "'" + node.name + "'";
  const DeclNode &decl = root(node.name, node.links, node);
  if (dynamic_cast<const ParamNode *>(&decl))
    error(node.line, node.col,
          "cannot assign to '" + target + "': " + object + " is a parameter");
  if (!decl.mut)
    error(node.line, node.col,
          "cannot assign to '" + target + "': " + object + " is not mut");
  const FieldNode *field = resolve_links(decl, node.links, node.line, true);
  if (is_self(decl))
    current_member->mutating = true;
  node.decl = &decl;
  node.type = field ? field->type_name : decl.type_name;
  const StructNode *replaced = field ? field->struct_type : decl.struct_type;
  if (replaced) {
    std::string inner = const_field(*replaced);
    if (!inner.empty())
      error(node.line, node.col,
            "cannot assign to '" + target + "': its field '" + inner +
                "' is not mut");
  }
  check(*node.value);
  check_assignable(*node.value, node.type, node, "assign to '" + target + "'");
}

void SemanticChecker::visit_call_stmt(CallStmtNode &node) {
  node.call->accept(*this);
}

void SemanticChecker::visit_exit(ExitNode &node) {
  if (current_fn && current_fn->result_type.empty()) {
    if (node.value)
      error(node.value->line, node.value->col,
            "function '" + current_fn->name +
                "' returns nothing, its exit takes no value");
    return;
  }
  if (!node.value) {
    if (current_fn)
      error(node.line, node.col,
            "function '" + current_fn->name + "' returns " +
                type_label(current_fn->result_type,
                           current_fn->result_struct) +
                ", its exit needs a value");
    error(node.line, node.col, "the exit of the program needs a value");
  }
  const std::string &type = check(*node.value);
  if (current_fn)
    check_assignable(*node.value, current_fn->result_type, *node.value,
                     "exit function '" + current_fn->name + "'");
  else if (!is_builtin(type))
    error(node.value->line, node.value->col,
          "cannot exit with a value of type " + type);
}

void SemanticChecker::visit_block(BlockNode &node) {
  scopes.push_back({});
  for (std::unique_ptr<StmtNode> &stmt : node.statements)
    stmt->accept(*this);
  if (node.exit)
    node.exit->accept(*this);
  scopes.pop_back();
}

void SemanticChecker::visit_if(IfNode &node) {
  const std::string &type = check(*node.cond);
  if (type != "bool")
    error(node.line, node.col,
          "the condition of 'if' must be bool, got " + type);
  node.then_block->accept(*this);
  if (node.else_block)
    node.else_block->accept(*this);
}

void SemanticChecker::visit_while(WhileNode &node) {
  const std::string &type = check(*node.cond);
  if (type != "bool")
    error(node.line, node.col,
          "the condition of 'while' must be bool, got " + type);
  node.body->accept(*this);
}

void SemanticChecker::visit_not(NotNode &node) {
  const std::string &type = check(*node.operand);
  if (type != "bool")
    error(node.line, node.col, "cannot apply '!' to " + type);
  node.type = "bool";
}

void SemanticChecker::visit_binop(BinOpNode &node) {
  const std::string &lt = check(*node.left);
  const std::string &rt = check(*node.right);
  if (node.op == "==" || node.op == "!=") {
    if (is_integer(lt) != is_integer(rt) || !is_builtin(lt) || !is_builtin(rt))
      error(node.line, node.col, "cannot compare " + lt + " with " + rt);
    node.type = "bool";
    return;
  }
  if (!is_integer(lt) || !is_integer(rt))
    error(node.line, node.col,
          "cannot apply '" + node.op + "' to " + (is_integer(lt) ? rt : lt));
  node.type = lt == "i64" || rt == "i64" ? "i64" : "i32";
}

void SemanticChecker::visit_var(VarNode &node) {
  node.decl = &root(node.name, node.links, node);
  const FieldNode *field =
      resolve_links(*node.decl, node.links, node.line, false);
  node.type = field ? field->type_name : node.decl->type_name;
}

void SemanticChecker::check_args(std::vector<std::unique_ptr<ExprNode>> &args,
                                 const FnNode &fn, const std::string &name,
                                 const ASTNode &at) {
  const std::vector<std::unique_ptr<ParamNode>> &params = fn.params;
  if (args.size() != params.size())
    error(at.line, at.col,
          "wrong number of arguments for '" + name + "': expected " +
              std::to_string(params.size()) + ", got " +
              std::to_string(args.size()));
  for (size_t i = 0; i < params.size(); i++) {
    check(*args[i]);
    check_assignable(*args[i], params[i]->type_name, *args[i],
                     "initialise parameter '" + params[i]->name + "'");
  }
}

void SemanticChecker::visit_call(CallNode &node) {
  const MemberFnNode *own =
      current_member ? method(*current_member->owner, node.name) : nullptr;
  if (own) {
    current_member->self_calls.push_back(own);
    node.fn = own;
  } else {
    auto it = functions.find(node.name);
    if (it == functions.end())
      error(node.line, node.col,
            "function '" + node.name + "' is not declared");
    node.fn = it->second;
  }
  note_call(*node.fn, node.name, node);
  check_args(node.args, *node.fn, node.name, node);
  node.type = node.fn->result_type;
}

void SemanticChecker::visit_member_call(MemberCallNode &node) {
  VarNode *object = dynamic_cast<VarNode *>(node.object.get());
  std::string target = show(*node.object);
  if (object && object->links.empty() && !find(object->name) &&
      !is_own_field(object->name) &&
      !(current_fn && globals.count(object->name)) &&
      structs.count(object->name))
    error(object->line, object->col,
          "cannot call '" + node.name + "' on struct '" + object->name +
              "': a member function is called on an object");
  const std::string &type = check(*node.object);
  auto owner = structs.find(type);
  if (owner == structs.end())
    error(node.line, object ? node.col : node.dot_col,
          "cannot call '" + node.name + "' on '" + target + "' of type " +
              type);
  node.fn = method(*owner->second, node.name);
  if (!node.fn)
    error(node.line, node.col,
          "struct " + type + " has no member function '" + node.name + "'");
  note_call(*node.fn, node.name, node);
  check_args(node.args, *node.fn, node.name, node);
  node.type = node.fn->result_type;
  if (!object)
    return;
  if (is_self(*object->decl))
    current_member->self_calls.push_back(node.fn);
  std::string blocker = const_link(*object);
  if (blocker.empty())
    return;
  if (mutation_known)
    check_mutation(node, target, blocker);
  else
    pending.push_back({&node, target, blocker});
}

void SemanticChecker::visit_field_of(FieldOfNode &node) {
  std::string target = show(*node.object);
  const std::string &type = check(*node.object);
  auto owner = structs.find(type);
  if (owner == structs.end())
    error(node.line, node.col,
          "'" + target + "' of type " + type + " has no fields");
  node.type = walk_links(target, type, owner->second, node.links, node.line,
                         nullptr)
                  ->type_name;
}

void SemanticChecker::visit_int(IntNode &node) {
  uint64_t v = 0;
  for (char c : node.text)
    v = v * 10 + (uint64_t)(c - '0');
  node.value = (int64_t)v;
  node.type = v > INT32_MAX ? "i64" : "i32";
}

void SemanticChecker::visit_bool(BoolNode &node) { node.type = "bool"; }

void SemanticChecker::visit_read(ReadNode &node) { node.type = "i32"; }

void SemanticChecker::visit_print(PrintNode &node) {
  if (!node.value)
    return;
  const std::string &type = check(*node.value);
  if (!is_builtin(type))
    node.struct_type = structs.at(type);
}
