#include "optimizer.h"

#include <iterator>
#include <map>
#include <set>

class EffectFinder {
  struct FnInfo {
    bool effect = false;
    std::set<const FnNode *> callees;
  };
  std::map<const FnNode *, FnInfo> fns;
  FnInfo *current = nullptr;

  void enter(const FnNode &fn) {
    FnInfo *outer = current;
    current = &fns[&fn];
    if (const MemberFnNode *m = dynamic_cast<const MemberFnNode *>(&fn))
      current->effect = m->mutating;
    scan(*fn.body);
    current = outer;
  }

  static bool writes_global(const ASTNode &node) {
    if (const AssignNode *assign = dynamic_cast<const AssignNode *>(&node))
      return assign->decl->global;
    if (const MemberCallNode *call = dynamic_cast<const MemberCallNode *>(&node))
      if (const VarNode *object = dynamic_cast<const VarNode *>(call->object.get()))
        return object->decl->global && call->fn->mutating;
    return false;
  }

  void scan(const ASTNode &node) {
    if (const FnNode *fn = dynamic_cast<const FnNode *>(&node)) {
      enter(*fn);
      return;
    }
    if (current) {
      if (dynamic_cast<const PrintNode *>(&node) ||
          dynamic_cast<const ReadNode *>(&node) ||
          dynamic_cast<const WhileNode *>(&node) ||
          dynamic_cast<const SpawnNode *>(&node) ||
          dynamic_cast<const LockNode *>(&node) || writes_global(node))
        current->effect = true;
      if (const CallNode *call = dynamic_cast<const CallNode *>(&node))
        current->callees.insert(call->fn);
      if (const MemberCallNode *call =
              dynamic_cast<const MemberCallNode *>(&node))
        current->callees.insert(call->fn);
    }
    for (const Child &child : node.children())
      scan(*child.second);
  }

  bool reaches(const FnNode *from, const FnNode *target,
               std::set<const FnNode *> &seen) const {
    auto it = fns.find(from);
    if (it == fns.end())
      return false;
    for (const FnNode *callee : it->second.callees) {
      if (callee == target)
        return true;
      if (seen.insert(callee).second && reaches(callee, target, seen))
        return true;
    }
    return false;
  }

public:
  bool recursive(const FnNode *fn) const {
    std::set<const FnNode *> seen;
    return reaches(fn, fn, seen);
  }

  std::set<const FnNode *> find(const ProgramNode &program) {
    scan(program);
    std::set<const FnNode *> effects;
    for (const auto &[fn, info] : fns)
      if (info.effect || recursive(fn))
        effects.insert(fn);
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto &[fn, info] : fns)
        for (const FnNode *callee : info.callees)
          if (!effects.count(fn) && effects.count(callee)) {
            effects.insert(fn);
            changed = true;
          }
    }
    return effects;
  }
};

class UseCounter : public Visitor {
  const std::set<const FnNode *> &effects;

  bool has_effect(const ASTNode &node) const {
    if (dynamic_cast<const ReadNode *>(&node))
      return true;
    if (const CallNode *call = dynamic_cast<const CallNode *>(&node))
      if (effects.count(call->fn))
        return true;
    if (const MemberCallNode *call =
            dynamic_cast<const MemberCallNode *>(&node))
      if (effects.count(call->fn))
        return true;
    for (const Child &child : node.children())
      if (has_effect(*child.second))
        return true;
    return false;
  }

  void read(const DeclNode *decl) { reads[decl]++; }

  template <class T> void visit_all(std::vector<std::unique_ptr<T>> &nodes) {
    for (std::unique_ptr<T> &node : nodes)
      node->accept(*this);
  }

public:
  std::map<const DeclNode *, int> reads;
  std::map<const FnNode *, int> calls;

  explicit UseCounter(const std::set<const FnNode *> &effects)
      : effects(effects) {}

  void visit_program(ProgramNode &node) override {
    visit_all(node.structs);
    visit_all(node.functions);
    visit_all(node.statements);
    node.exit->accept(*this);
  }

  void visit_struct(StructNode &node) override { visit_all(node.methods); }

  void visit_fn(FnNode &node) override {
    calls.emplace(&node, 0);
    node.body->accept(*this);
  }

  void visit_member_fn(MemberFnNode &node) override {
    node.body->accept(*this);
  }

  void visit_decl(DeclNode &node) override {
    reads.emplace(&node, 0);
    if (has_effect(*node.init))
      read(&node);
    node.init->accept(*this);
  }

  void visit_assign(AssignNode &node) override {
    if (has_effect(*node.value) || (node.index && has_effect(*node.index)))
      read(node.decl);
    if (node.index)
      node.index->accept(*this);
    node.value->accept(*this);
  }

  void visit_call_stmt(CallStmtNode &node) override {
    node.call->accept(*this);
  }

  void visit_exit(ExitNode &node) override {
    if (node.value)
      node.value->accept(*this);
  }

  void visit_block(BlockNode &node) override {
    visit_all(node.statements);
    if (node.exit)
      node.exit->accept(*this);
  }

  void visit_if(IfNode &node) override {
    node.cond->accept(*this);
    node.then_block->accept(*this);
    if (node.else_block)
      node.else_block->accept(*this);
  }

  void visit_while(WhileNode &node) override {
    node.cond->accept(*this);
    node.body->accept(*this);
  }

  void visit_binop(BinOpNode &node) override {
    node.left->accept(*this);
    node.right->accept(*this);
  }

  void visit_not(NotNode &node) override { node.operand->accept(*this); }

  void visit_var(VarNode &node) override { read(node.decl); }

  void visit_call(CallNode &node) override {
    calls[node.fn]++;
    visit_all(node.args);
  }

  void visit_member_call(MemberCallNode &node) override {
    node.object->accept(*this);
    visit_all(node.args);
  }

  void visit_field_of(FieldOfNode &node) override {
    node.object->accept(*this);
  }

  void visit_init(InitNode &node) override { visit_all(node.values); }

  void visit_read(ReadNode &) override {}

  void visit_print(PrintNode &node) override {
    if (node.value)
      node.value->accept(*this);
  }

  void visit_int(IntNode &) override {}

  void visit_bool(BoolNode &) override {}

  void visit_array(ArrayNode &node) override {
    if (node.fill)
      node.fill->accept(*this);
  }

  void visit_index(IndexNode &node) override {
    node.array->accept(*this);
    node.index->accept(*this);
  }

  void visit_thread_index(ThreadIndexNode &) override {}

  void visit_spawn(SpawnNode &node) override { visit_all(node.lines); }

  void visit_range(RangeNode &node) override {
    node.count->accept(*this);
    node.call->accept(*this);
  }

  void visit_lock(LockNode &node) override { node.body->accept(*this); }
};

class Pruner {
  const std::set<const DeclNode *> &dead;
  const std::set<const FnNode *> &dead_fns;

  bool removed(const StmtNode &stmt) const {
    if (const AssignNode *assign = dynamic_cast<const AssignNode *>(&stmt))
      return dead.count(assign->decl);
    if (const SpawnNode *spawn = dynamic_cast<const SpawnNode *>(&stmt))
      return spawn->lines.empty();
    const DeclNode *decl = dynamic_cast<const DeclNode *>(&stmt);
    return decl && dead.count(decl);
  }

  void prune(std::vector<std::unique_ptr<StmtNode>> &statements) {
    std::vector<std::unique_ptr<StmtNode>> kept;
    for (std::unique_ptr<StmtNode> &stmt : statements) {
      prune_inside(*stmt);
      if (removed(*stmt))
        continue;
      kept.push_back(std::move(stmt));
    }
    statements = std::move(kept);
  }

  void prune_inside(StmtNode &stmt) {
    if (IfNode *node = dynamic_cast<IfNode *>(&stmt)) {
      prune(node->then_block->statements);
      if (node->else_block)
        prune(node->else_block->statements);
    } else if (WhileNode *node = dynamic_cast<WhileNode *>(&stmt)) {
      prune(node->body->statements);
    } else if (LockNode *node = dynamic_cast<LockNode *>(&stmt)) {
      prune(node->body->statements);
    } else if (SpawnNode *node = dynamic_cast<SpawnNode *>(&stmt)) {
      prune(node->lines);
    }
  }

public:
  Pruner(const std::set<const DeclNode *> &dead,
         const std::set<const FnNode *> &dead_fns)
      : dead(dead), dead_fns(dead_fns) {}

  void prune(ProgramNode &program) {
    for (std::unique_ptr<StructNode> &s : program.structs)
      for (std::unique_ptr<MemberFnNode> &m : s->methods)
        prune(m->body->statements);
    std::vector<std::unique_ptr<FnNode>> kept;
    for (std::unique_ptr<FnNode> &fn : program.functions) {
      if (dead_fns.count(fn.get()))
        continue;
      prune(fn->body->statements);
      kept.push_back(std::move(fn));
    }
    program.functions = std::move(kept);
    prune(program.statements);
  }
};

template <class Key>
static std::set<const Key *> unused(const std::map<const Key *, int> &counts) {
  std::set<const Key *> dead;
  for (const auto &[key, count] : counts)
    if (count == 0)
      dead.insert(key);
  return dead;
}

static void remove_unused(ProgramNode &program) {
  while (true) {
    std::set<const FnNode *> effects = EffectFinder().find(program);
    UseCounter counter(effects);
    program.accept(counter);
    std::set<const DeclNode *> dead = unused(counter.reads);
    std::set<const FnNode *> dead_fns = unused(counter.calls);
    if (dead.empty() && dead_fns.empty())
      return;
    Pruner(dead, dead_fns).prune(program);
  }
}

static std::string fresh(const std::string &name, int &counter) {
  return name.substr(0, name.find('#')) + "#" + std::to_string(++counter);
}

class Cloner {
  int &counter;
  std::map<const DeclNode *, const DeclNode *> renamed;

  template <class T>
  std::vector<std::unique_ptr<T>>
  all(const std::vector<std::unique_ptr<T>> &nodes) {
    std::vector<std::unique_ptr<T>> out;
    for (const std::unique_ptr<T> &node : nodes)
      out.push_back(clone(*node));
    return out;
  }

  std::unique_ptr<ExprNode> clone(const ExprNode &node) { return expr(node); }
  std::unique_ptr<StmtNode> clone(const StmtNode &node) { return stmt(node); }

  const DeclNode *target(const DeclNode *decl) const {
    auto it = renamed.find(decl);
    return it == renamed.end() ? decl : it->second;
  }

  std::unique_ptr<ExprNode> copy(const ExprNode &node) {
    if (const BinOpNode *n = dynamic_cast<const BinOpNode *>(&node))
      return std::make_unique<BinOpNode>(n->line, n->col, n->op,
                                         expr(*n->left), expr(*n->right));
    if (const NotNode *n = dynamic_cast<const NotNode *>(&node))
      return std::make_unique<NotNode>(n->line, n->col, expr(*n->operand));
    if (const VarNode *n = dynamic_cast<const VarNode *>(&node)) {
      auto out = std::make_unique<VarNode>(n->line, n->col, n->name, n->links);
      out->decl = target(n->decl);
      if (out->decl != n->decl)
        out->name = out->decl->name;
      return out;
    }
    if (const CallNode *n = dynamic_cast<const CallNode *>(&node)) {
      auto out =
          std::make_unique<CallNode>(n->line, n->col, n->name, all(n->args));
      out->fn = n->fn;
      return out;
    }
    if (const MemberCallNode *n = dynamic_cast<const MemberCallNode *>(&node)) {
      auto out = std::make_unique<MemberCallNode>(
          n->line, n->col, expr(*n->object), n->dot_col, n->name, all(n->args));
      out->fn = n->fn;
      return out;
    }
    if (const FieldOfNode *n = dynamic_cast<const FieldOfNode *>(&node))
      return std::make_unique<FieldOfNode>(n->line, n->col, expr(*n->object),
                                           n->links);
    if (const InitNode *n = dynamic_cast<const InitNode *>(&node))
      return init(*n);
    if (dynamic_cast<const ReadNode *>(&node))
      return std::make_unique<ReadNode>(node.line, node.col);
    if (const IntNode *n = dynamic_cast<const IntNode *>(&node)) {
      auto out = std::make_unique<IntNode>(n->line, n->col, n->text);
      out->value = n->value;
      return out;
    }
    if (const BoolNode *n = dynamic_cast<const BoolNode *>(&node))
      return std::make_unique<BoolNode>(n->line, n->col, n->value);
    if (const ArrayNode *n = dynamic_cast<const ArrayNode *>(&node))
      return std::make_unique<ArrayNode>(n->line, n->col, n->element, n->size,
                                         n->fill ? expr(*n->fill) : nullptr);
    if (const IndexNode *n = dynamic_cast<const IndexNode *>(&node))
      return std::make_unique<IndexNode>(n->line, n->col, expr(*n->array),
                                         expr(*n->index));
    return std::make_unique<ThreadIndexNode>(node.line, node.col);
  }

public:
  explicit Cloner(int &counter) : counter(counter) {}

  void bind(const DeclNode *from, const DeclNode *to) { renamed[from] = to; }

  std::unique_ptr<ExprNode> expr(const ExprNode &node) {
    std::unique_ptr<ExprNode> out = copy(node);
    out->type = node.type;
    return out;
  }

  std::unique_ptr<InitNode> init(const InitNode &node) {
    auto out = std::make_unique<InitNode>(node.line, node.col, all(node.values));
    out->type = node.type;
    out->struct_type = node.struct_type;
    out->copy = node.copy;
    return out;
  }

  std::unique_ptr<BlockNode> block(const BlockNode &node) {
    std::unique_ptr<ExitNode> exit;
    if (node.exit)
      exit = std::make_unique<ExitNode>(
          node.exit->line, node.exit->col,
          node.exit->value ? expr(*node.exit->value) : nullptr);
    return std::make_unique<BlockNode>(node.line, node.col,
                                       all(node.statements), std::move(exit),
                                       node.end_line, node.end_col);
  }

  std::unique_ptr<StmtNode> stmt(const StmtNode &node) {
    if (const DeclNode *n = dynamic_cast<const DeclNode *>(&node)) {
      auto out = std::make_unique<DeclNode>(n->line, n->col, n->type_name,
                                            n->is_struct, n->type_col,
                                            fresh(n->name, counter), n->mut,
                                            init(*n->init));
      out->struct_type = n->struct_type;
      bind(n, out.get());
      return out;
    }
    if (const AssignNode *n = dynamic_cast<const AssignNode *>(&node)) {
      auto out = std::make_unique<AssignNode>(
          n->line, n->col, n->name, n->links, expr(*n->value),
          n->index ? expr(*n->index) : nullptr, n->index_col);
      out->decl = target(n->decl);
      if (out->decl != n->decl)
        out->name = out->decl->name;
      out->type = n->type;
      return out;
    }
    if (const CallStmtNode *n = dynamic_cast<const CallStmtNode *>(&node))
      return std::make_unique<CallStmtNode>(n->line, n->col, expr(*n->call));
    if (const PrintNode *n = dynamic_cast<const PrintNode *>(&node)) {
      auto out = std::make_unique<PrintNode>(
          n->line, n->col, n->value ? expr(*n->value) : nullptr, n->text);
      out->struct_type = n->struct_type;
      return out;
    }
    if (const IfNode *n = dynamic_cast<const IfNode *>(&node))
      return std::make_unique<IfNode>(
          n->line, n->col, expr(*n->cond), block(*n->then_block),
          n->else_block ? block(*n->else_block) : nullptr);
    if (const WhileNode *n = dynamic_cast<const WhileNode *>(&node))
      return std::make_unique<WhileNode>(n->line, n->col, expr(*n->cond),
                                         block(*n->body));
    if (const SpawnNode *n = dynamic_cast<const SpawnNode *>(&node))
      return std::make_unique<SpawnNode>(n->line, n->col, all(n->lines));
    if (const RangeNode *n = dynamic_cast<const RangeNode *>(&node)) {
      std::unique_ptr<CallNode> call(
          static_cast<CallNode *>(expr(*n->call).release()));
      return std::make_unique<RangeNode>(n->line, n->col, expr(*n->count),
                                         std::move(call));
    }
    const LockNode &n = static_cast<const LockNode &>(node);
    auto out = std::make_unique<LockNode>(n.line, n.col, n.name, n.name_col,
                                          block(*n.body));
    out->mutex = n.mutex;
    return out;
  }
};

class Inliner {
  ProgramNode &program;
  const FnNode &target;
  int &counter;

  static std::unique_ptr<ExprNode> *slot(StmtNode &stmt) {
    if (DeclNode *n = dynamic_cast<DeclNode *>(&stmt))
      return n->init->values.size() == 1 ? &n->init->values[0] : nullptr;
    if (AssignNode *n = dynamic_cast<AssignNode *>(&stmt))
      return &n->value;
    if (CallStmtNode *n = dynamic_cast<CallStmtNode *>(&stmt))
      return &n->call;
    if (PrintNode *n = dynamic_cast<PrintNode *>(&stmt))
      return n->value ? &n->value : nullptr;
    if (IfNode *n = dynamic_cast<IfNode *>(&stmt))
      return &n->cond;
    return nullptr;
  }

  CallNode *call_at(std::unique_ptr<ExprNode> *slot) const {
    if (!slot || !*slot)
      return nullptr;
    CallNode *call = dynamic_cast<CallNode *>(slot->get());
    return call && call->fn == &target ? call : nullptr;
  }

  const StructNode *find_struct(const std::string &name) const {
    for (const std::unique_ptr<StructNode> &s : program.structs)
      if (s->name == name)
        return s.get();
    return nullptr;
  }

  std::unique_ptr<DeclNode> local(const ASTNode &at, const std::string &type,
                                  bool is_struct, uint64_t type_col,
                                  const std::string &name,
                                  std::unique_ptr<ExprNode> value) {
    std::vector<std::unique_ptr<ExprNode>> values;
    values.push_back(std::move(value));
    auto init = std::make_unique<InitNode>(at.line, at.col, std::move(values));
    auto decl = std::make_unique<DeclNode>(at.line, at.col, type, is_struct,
                                           type_col, name, false,
                                           std::move(init));
    decl->struct_type = is_struct ? find_struct(type) : nullptr;
    decl->init->type = type;
    decl->init->struct_type = decl->struct_type;
    decl->init->copy = is_struct || is_array_type(type);
    return decl;
  }

  std::unique_ptr<ExprNode> expand(CallNode &call,
                                   std::vector<std::unique_ptr<StmtNode>> &out,
                                   bool discard) {
    Cloner cloner(counter);
    for (size_t i = 0; i < target.params.size(); i++) {
      const ParamNode &param = *target.params[i];
      std::unique_ptr<DeclNode> decl =
          local(param, param.type_name, param.is_struct, param.type_col,
                fresh(param.name, counter), std::move(call.args[i]));
      cloner.bind(&param, decl.get());
      out.push_back(std::move(decl));
    }
    for (const std::unique_ptr<StmtNode> &stmt : target.body->statements)
      out.push_back(cloner.stmt(*stmt));
    const ExitNode *exit = target.body->exit.get();
    if (!exit || !exit->value)
      return nullptr;
    std::unique_ptr<ExprNode> value = cloner.expr(*exit->value);
    if (!discard)
      return value;
    out.push_back(local(*exit, target.result_type, target.result_struct,
                        target.result_col, fresh(target.name, counter),
                        std::move(value)));
    return nullptr;
  }

  bool rewrite(std::vector<std::unique_ptr<StmtNode>> &statements,
               std::unique_ptr<ExitNode> *exit) {
    for (size_t i = 0; i < statements.size(); i++) {
      StmtNode &stmt = *statements[i];
      std::unique_ptr<ExprNode> *at = slot(stmt);
      if (CallNode *call = call_at(at)) {
        bool alone = dynamic_cast<CallStmtNode *>(&stmt);
        std::vector<std::unique_ptr<StmtNode>> body;
        std::unique_ptr<ExprNode> value = expand(*call, body, alone);
        if (alone)
          statements.erase(statements.begin() + i);
        else
          *at = std::move(value);
        statements.insert(statements.begin() + i,
                          std::make_move_iterator(body.begin()),
                          std::make_move_iterator(body.end()));
        return true;
      }
      if (descend(stmt))
        return true;
    }
    if (!exit || !*exit)
      return false;
    CallNode *call = call_at(&(*exit)->value);
    if (!call)
      return false;
    std::vector<std::unique_ptr<StmtNode>> body;
    (*exit)->value = expand(*call, body, false);
    for (std::unique_ptr<StmtNode> &stmt : body)
      statements.push_back(std::move(stmt));
    return true;
  }

  bool rewrite(BlockNode &block) {
    return rewrite(block.statements, &block.exit);
  }

  bool descend(StmtNode &stmt) {
    if (IfNode *n = dynamic_cast<IfNode *>(&stmt))
      return rewrite(*n->then_block) ||
             (n->else_block && rewrite(*n->else_block));
    if (WhileNode *n = dynamic_cast<WhileNode *>(&stmt))
      return rewrite(*n->body);
    if (LockNode *n = dynamic_cast<LockNode *>(&stmt))
      return rewrite(*n->body);
    return false;
  }

public:
  Inliner(ProgramNode &program, const FnNode &target, int &counter)
      : program(program), target(target), counter(counter) {}

  bool run() {
    for (std::unique_ptr<StructNode> &s : program.structs)
      for (std::unique_ptr<MemberFnNode> &m : s->methods)
        if (rewrite(*m->body))
          return true;
    for (std::unique_ptr<FnNode> &fn : program.functions)
      if (fn.get() != &target && rewrite(*fn->body))
        return true;
    return rewrite(program.statements, &program.exit);
  }
};

static bool exits_inside(const ASTNode &node) {
  if (dynamic_cast<const ExitNode *>(&node))
    return true;
  for (const Child &child : node.children())
    if (exits_inside(*child.second))
      return true;
  return false;
}

static void collect_spawned(const ASTNode &node,
                            std::set<const FnNode *> &spawned) {
  if (const SpawnNode *spawn = dynamic_cast<const SpawnNode *>(&node)) {
    for (const std::unique_ptr<StmtNode> &line : spawn->lines)
      spawned.insert(
          static_cast<const CallNode *>(line->children().back().second)->fn);
  }
  for (const Child &child : node.children())
    collect_spawned(*child.second, spawned);
}

static bool inlinable(const FnNode &fn) {
  for (const std::unique_ptr<StmtNode> &stmt : fn.body->statements)
    if (exits_inside(*stmt))
      return false;
  return true;
}

static bool inline_one(ProgramNode &program, int &counter) {
  EffectFinder finder;
  std::set<const FnNode *> effects = finder.find(program);
  UseCounter uses(effects);
  program.accept(uses);
  std::set<const FnNode *> spawned;
  collect_spawned(program, spawned);
  for (std::unique_ptr<FnNode> &fn : program.functions) {
    if (uses.calls[fn.get()] != 1 || spawned.count(fn.get()) ||
        finder.recursive(fn.get()) || !inlinable(*fn))
      continue;
    if (Inliner(program, *fn, counter).run())
      return true;
  }
  return false;
}

void optimize(ProgramNode &program) {
  int counter = 0;
  remove_unused(program);
  while (inline_one(program, counter))
    remove_unused(program);
}
