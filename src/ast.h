#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class ProgramNode;
class StructNode;
class FnNode;
class MemberFnNode;
class BlockNode;
class ExitNode;
class DeclNode;
class AssignNode;
class CallStmtNode;
class PrintNode;
class IfNode;
class WhileNode;
class BinOpNode;
class NotNode;
class VarNode;
class CallNode;
class MemberCallNode;
class FieldOfNode;
class InitNode;
class ReadNode;
class IntNode;
class BoolNode;

class Visitor {
public:
  virtual ~Visitor() = default;
  virtual void visit_program(ProgramNode &node) = 0;
  virtual void visit_struct(StructNode &node) = 0;
  virtual void visit_fn(FnNode &node) = 0;
  virtual void visit_member_fn(MemberFnNode &node) = 0;
  virtual void visit_block(BlockNode &node) = 0;
  virtual void visit_exit(ExitNode &node) = 0;
  virtual void visit_decl(DeclNode &node) = 0;
  virtual void visit_assign(AssignNode &node) = 0;
  virtual void visit_call_stmt(CallStmtNode &node) = 0;
  virtual void visit_print(PrintNode &node) = 0;
  virtual void visit_if(IfNode &node) = 0;
  virtual void visit_while(WhileNode &node) = 0;
  virtual void visit_binop(BinOpNode &node) = 0;
  virtual void visit_not(NotNode &node) = 0;
  virtual void visit_var(VarNode &node) = 0;
  virtual void visit_call(CallNode &node) = 0;
  virtual void visit_member_call(MemberCallNode &node) = 0;
  virtual void visit_field_of(FieldOfNode &node) = 0;
  virtual void visit_init(InitNode &node) = 0;
  virtual void visit_read(ReadNode &node) = 0;
  virtual void visit_int(IntNode &node) = 0;
  virtual void visit_bool(BoolNode &node) = 0;
};

class ASTNode;
using Child = std::pair<std::string, const ASTNode *>;

class ASTNode {
public:
  uint64_t line, col;
  ASTNode(uint64_t line, uint64_t col) : line(line), col(col) {}
  virtual ~ASTNode() = default;
  virtual void accept(Visitor &visitor) = 0;
  virtual std::string label() const = 0;
  virtual std::vector<Child> children() const { return {}; }

  void dump(int depth = 0, const std::string &role = "") const {
    std::cout << std::string(depth * 2, ' ') << (role.empty() ? "" : role + ": ")
              << label() << " [" << line << ":" << col << "]\n";
    for (const Child &child : children())
      child.second->dump(depth + 1, child.first);
  }
};

struct Link {
  std::string name;
  uint64_t col;
  unsigned index = 0;
};

inline std::string chain(std::string name, const std::vector<Link> &links) {
  for (const Link &link : links)
    name += "." + link.name;
  return name;
}

inline std::string type_label(const std::string &type_name, bool is_struct) {
  if (type_name.empty())
    return "none";
  return (is_struct ? "struct " : "") + type_name;
}

inline std::string mut_label(bool mut) { return mut ? " mut" : " const"; }

template <class T>
void add_children(std::vector<Child> &c, const std::string &role,
                  const std::vector<std::unique_ptr<T>> &nodes) {
  for (const std::unique_ptr<T> &node : nodes)
    c.push_back({role, node.get()});
}

class ExprNode : public ASTNode {
public:
  std::string type;
  using ASTNode::ASTNode;
};

class BinOpNode : public ExprNode {
public:
  std::string op;
  std::unique_ptr<ExprNode> left, right;
  BinOpNode(uint64_t line, uint64_t col, std::string op,
            std::unique_ptr<ExprNode> left, std::unique_ptr<ExprNode> right)
      : ExprNode(line, col), op(std::move(op)), left(std::move(left)),
        right(std::move(right)) {}
  void accept(Visitor &visitor) override { visitor.visit_binop(*this); }
  std::string label() const override { return "BinOp " + op; }
  std::vector<Child> children() const override {
    return {{"left", left.get()}, {"right", right.get()}};
  }
};

class NotNode : public ExprNode {
public:
  std::unique_ptr<ExprNode> operand;
  NotNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> operand)
      : ExprNode(line, col), operand(std::move(operand)) {}
  void accept(Visitor &visitor) override { visitor.visit_not(*this); }
  std::string label() const override { return "Not"; }
  std::vector<Child> children() const override {
    return {{"operand", operand.get()}};
  }
};

class VarNode : public ExprNode {
public:
  std::string name;
  std::vector<Link> links;
  const DeclNode *decl = nullptr;
  VarNode(uint64_t line, uint64_t col, std::string name,
          std::vector<Link> links = {})
      : ExprNode(line, col), name(std::move(name)), links(std::move(links)) {}
  void accept(Visitor &visitor) override { visitor.visit_var(*this); }
  std::string label() const override { return "Var " + chain(name, links); }
};

class CallNode : public ExprNode {
public:
  std::string name;
  std::vector<std::unique_ptr<ExprNode>> args;
  const FnNode *fn = nullptr;
  CallNode(uint64_t line, uint64_t col, std::string name,
           std::vector<std::unique_ptr<ExprNode>> args)
      : ExprNode(line, col), name(std::move(name)), args(std::move(args)) {}
  void accept(Visitor &visitor) override { visitor.visit_call(*this); }
  std::string label() const override {
    return "Call " + name + " args " + std::to_string(args.size());
  }
  std::vector<Child> children() const override {
    std::vector<Child> c;
    add_children(c, "arg", args);
    return c;
  }
};

class MemberCallNode : public ExprNode {
public:
  std::unique_ptr<ExprNode> object;
  uint64_t dot_col;
  std::string name;
  std::vector<std::unique_ptr<ExprNode>> args;
  const MemberFnNode *fn = nullptr;
  MemberCallNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> object,
                 uint64_t dot_col, std::string name,
                 std::vector<std::unique_ptr<ExprNode>> args)
      : ExprNode(line, col), object(std::move(object)), dot_col(dot_col),
        name(std::move(name)), args(std::move(args)) {}
  void accept(Visitor &visitor) override { visitor.visit_member_call(*this); }
  std::string label() const override {
    return "MemberCall " + name + " args " + std::to_string(args.size());
  }
  std::vector<Child> children() const override {
    std::vector<Child> c = {{"object", object.get()}};
    add_children(c, "arg", args);
    return c;
  }
};

class FieldOfNode : public ExprNode {
public:
  std::unique_ptr<ExprNode> object;
  std::vector<Link> links;
  FieldOfNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> object,
              std::vector<Link> links)
      : ExprNode(line, col), object(std::move(object)),
        links(std::move(links)) {}
  void accept(Visitor &visitor) override { visitor.visit_field_of(*this); }
  std::string label() const override {
    std::vector<Link> rest(links.begin() + 1, links.end());
    return "FieldOf " + chain(links.front().name, rest);
  }
  std::vector<Child> children() const override {
    return {{"object", object.get()}};
  }
};

class InitNode : public ExprNode {
public:
  std::vector<std::unique_ptr<ExprNode>> values;
  const StructNode *struct_type = nullptr;
  bool copy = false;
  InitNode(uint64_t line, uint64_t col,
           std::vector<std::unique_ptr<ExprNode>> values)
      : ExprNode(line, col), values(std::move(values)) {}
  void accept(Visitor &visitor) override { visitor.visit_init(*this); }
  std::string label() const override { return "Init"; }
  std::vector<Child> children() const override {
    std::vector<Child> c;
    add_children(c, "value", values);
    return c;
  }
};

class ReadNode : public ExprNode {
public:
  using ExprNode::ExprNode;
  void accept(Visitor &visitor) override { visitor.visit_read(*this); }
  std::string label() const override { return "Read"; }
};

class IntNode : public ExprNode {
public:
  std::string text;
  int64_t value = 0;
  IntNode(uint64_t line, uint64_t col, std::string text)
      : ExprNode(line, col), text(std::move(text)) {}
  void accept(Visitor &visitor) override { visitor.visit_int(*this); }
  std::string label() const override { return "Int " + text; }
};

class BoolNode : public ExprNode {
public:
  bool value;
  BoolNode(uint64_t line, uint64_t col, bool value)
      : ExprNode(line, col), value(value) {}
  void accept(Visitor &visitor) override { visitor.visit_bool(*this); }
  std::string label() const override {
    return std::string("Bool ") + (value ? "true" : "false");
  }
};

class StmtNode : public ASTNode {
public:
  using ASTNode::ASTNode;
};

class DeclNode : public StmtNode {
public:
  std::string type_name;
  bool is_struct;
  uint64_t type_col;
  std::string name;
  bool mut;
  std::unique_ptr<InitNode> init;
  const StructNode *struct_type = nullptr;
  bool global = false;
  DeclNode(uint64_t line, uint64_t col, std::string type_name, bool is_struct,
           uint64_t type_col, std::string name, bool mut,
           std::unique_ptr<InitNode> init)
      : StmtNode(line, col), type_name(std::move(type_name)),
        is_struct(is_struct), type_col(type_col), name(std::move(name)),
        mut(mut), init(std::move(init)) {}
  void accept(Visitor &visitor) override { visitor.visit_decl(*this); }
  std::string label() const override {
    return "Decl " + name + " " + type_label(type_name, is_struct) +
           mut_label(mut);
  }
  std::vector<Child> children() const override {
    std::vector<Child> c;
    add_children(c, "init", init->values);
    return c;
  }
};

class ParamNode : public DeclNode {
public:
  ParamNode(uint64_t line, uint64_t col, std::string type_name, bool is_struct,
            uint64_t type_col, std::string name)
      : DeclNode(line, col, std::move(type_name), is_struct, type_col,
                 std::move(name), false, nullptr) {}
  void accept(Visitor &) override {}
  std::string label() const override {
    return "Param " + name + " " + type_label(type_name, is_struct);
  }
  std::vector<Child> children() const override { return {}; }
};

class FieldNode : public ASTNode {
public:
  std::string type_name;
  bool is_struct;
  uint64_t type_col;
  std::string name;
  bool mut;
  const StructNode *struct_type = nullptr;
  FieldNode(uint64_t line, uint64_t col, std::string type_name, bool is_struct,
            uint64_t type_col, std::string name, bool mut)
      : ASTNode(line, col), type_name(std::move(type_name)),
        is_struct(is_struct), type_col(type_col), name(std::move(name)),
        mut(mut) {}
  void accept(Visitor &) override {}
  std::string label() const override {
    return "Field " + name + " " + type_label(type_name, is_struct) +
           mut_label(mut);
  }
};

class AssignNode : public StmtNode {
public:
  std::string name;
  std::vector<Link> links;
  std::unique_ptr<ExprNode> value;
  const DeclNode *decl = nullptr;
  std::string type;
  AssignNode(uint64_t line, uint64_t col, std::string name,
             std::vector<Link> links, std::unique_ptr<ExprNode> value)
      : StmtNode(line, col), name(std::move(name)), links(std::move(links)),
        value(std::move(value)) {}
  void accept(Visitor &visitor) override { visitor.visit_assign(*this); }
  std::string label() const override {
    return "Assign " + chain(name, links);
  }
  std::vector<Child> children() const override {
    return {{"value", value.get()}};
  }
};

class CallStmtNode : public StmtNode {
public:
  std::unique_ptr<ExprNode> call;
  CallStmtNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> call)
      : StmtNode(line, col), call(std::move(call)) {}
  void accept(Visitor &visitor) override { visitor.visit_call_stmt(*this); }
  std::string label() const override { return "CallStmt"; }
  std::vector<Child> children() const override {
    return {{"call", call.get()}};
  }
};

class PrintNode : public StmtNode {
public:
  std::unique_ptr<ExprNode> value;
  std::string text;
  const StructNode *struct_type = nullptr;
  PrintNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> value,
            std::string text)
      : StmtNode(line, col), value(std::move(value)), text(std::move(text)) {}
  void accept(Visitor &visitor) override { visitor.visit_print(*this); }
  std::string label() const override {
    return value ? "Print" : "Print \"" + text + "\"";
  }
  std::vector<Child> children() const override {
    if (value)
      return {{"value", value.get()}};
    return {};
  }
};

class ExitNode : public ASTNode {
public:
  std::unique_ptr<ExprNode> value;
  ExitNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> value)
      : ASTNode(line, col), value(std::move(value)) {}
  void accept(Visitor &visitor) override { visitor.visit_exit(*this); }
  std::string label() const override { return "Exit"; }
  std::vector<Child> children() const override {
    if (value)
      return {{"value", value.get()}};
    return {};
  }
};

class BlockNode : public ASTNode {
public:
  std::vector<std::unique_ptr<StmtNode>> statements;
  std::unique_ptr<ExitNode> exit;
  uint64_t end_line, end_col;
  BlockNode(uint64_t line, uint64_t col,
            std::vector<std::unique_ptr<StmtNode>> statements,
            std::unique_ptr<ExitNode> exit, uint64_t end_line,
            uint64_t end_col)
      : ASTNode(line, col), statements(std::move(statements)),
        exit(std::move(exit)), end_line(end_line), end_col(end_col) {}
  void accept(Visitor &visitor) override { visitor.visit_block(*this); }
  std::string label() const override { return "Block"; }
  std::vector<Child> children() const override {
    std::vector<Child> c;
    add_children(c, "", statements);
    if (exit)
      c.push_back({"", exit.get()});
    return c;
  }
};

class IfNode : public StmtNode {
public:
  std::unique_ptr<ExprNode> cond;
  std::unique_ptr<BlockNode> then_block, else_block;
  IfNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> cond,
         std::unique_ptr<BlockNode> then_block,
         std::unique_ptr<BlockNode> else_block)
      : StmtNode(line, col), cond(std::move(cond)),
        then_block(std::move(then_block)), else_block(std::move(else_block)) {}
  void accept(Visitor &visitor) override { visitor.visit_if(*this); }
  std::string label() const override { return "If"; }
  std::vector<Child> children() const override {
    std::vector<Child> c = {{"cond", cond.get()}, {"then", then_block.get()}};
    if (else_block)
      c.push_back({"else", else_block.get()});
    return c;
  }
};

class WhileNode : public StmtNode {
public:
  std::unique_ptr<ExprNode> cond;
  std::unique_ptr<BlockNode> body;
  WhileNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> cond,
            std::unique_ptr<BlockNode> body)
      : StmtNode(line, col), cond(std::move(cond)), body(std::move(body)) {}
  void accept(Visitor &visitor) override { visitor.visit_while(*this); }
  std::string label() const override { return "While"; }
  std::vector<Child> children() const override {
    return {{"cond", cond.get()}, {"body", body.get()}};
  }
};

class FnNode : public ASTNode {
public:
  std::string name;
  std::vector<std::unique_ptr<ParamNode>> params;
  std::string result_type;
  bool result_struct;
  uint64_t result_col;
  std::unique_ptr<BlockNode> body;
  FnNode(uint64_t line, uint64_t col, std::string name,
         std::vector<std::unique_ptr<ParamNode>> params,
         std::string result_type, bool result_struct, uint64_t result_col,
         std::unique_ptr<BlockNode> body)
      : ASTNode(line, col), name(std::move(name)), params(std::move(params)),
        result_type(std::move(result_type)), result_struct(result_struct),
        result_col(result_col), body(std::move(body)) {}
  void accept(Visitor &visitor) override { visitor.visit_fn(*this); }
  std::string label() const override {
    return "Fn " + name + " result " + type_label(result_type, result_struct);
  }
  std::vector<Child> children() const override {
    std::vector<Child> c;
    add_children(c, "param", params);
    c.push_back({"body", body.get()});
    return c;
  }
};

class MemberFnNode : public FnNode {
public:
  const StructNode *owner = nullptr;
  std::unique_ptr<DeclNode> self;
  bool mutating = false;
  std::vector<const MemberFnNode *> self_calls;
  using FnNode::FnNode;
  void accept(Visitor &visitor) override { visitor.visit_member_fn(*this); }
  std::string label() const override {
    return "MemberFn " + name + " result " +
           type_label(result_type, result_struct);
  }
};

class StructNode : public ASTNode {
public:
  std::string name;
  std::vector<std::unique_ptr<FieldNode>> fields;
  std::vector<std::unique_ptr<MemberFnNode>> methods;
  StructNode(uint64_t line, uint64_t col, std::string name,
             std::vector<std::unique_ptr<FieldNode>> fields,
             std::vector<std::unique_ptr<MemberFnNode>> methods)
      : ASTNode(line, col), name(std::move(name)), fields(std::move(fields)),
        methods(std::move(methods)) {
    for (std::unique_ptr<MemberFnNode> &m : this->methods) {
      m->owner = this;
      m->self = std::make_unique<DeclNode>(m->line, m->col, this->name, true,
                                           m->col, "this", true, nullptr);
      m->self->struct_type = this;
    }
  }
  void accept(Visitor &visitor) override { visitor.visit_struct(*this); }
  std::string label() const override { return "Struct " + name; }
  std::vector<Child> children() const override {
    std::vector<Child> c;
    add_children(c, "field", fields);
    add_children(c, "method", methods);
    return c;
  }
};

class ProgramNode : public ASTNode {
public:
  std::vector<std::unique_ptr<StructNode>> structs;
  std::vector<std::unique_ptr<FnNode>> functions;
  std::vector<std::unique_ptr<StmtNode>> statements;
  std::unique_ptr<ExitNode> exit;
  ProgramNode(uint64_t line, uint64_t col,
              std::vector<std::unique_ptr<StructNode>> structs,
              std::vector<std::unique_ptr<FnNode>> functions,
              std::vector<std::unique_ptr<StmtNode>> statements,
              std::unique_ptr<ExitNode> exit)
      : ASTNode(line, col), structs(std::move(structs)),
        functions(std::move(functions)), statements(std::move(statements)),
        exit(std::move(exit)) {}
  void accept(Visitor &visitor) override { visitor.visit_program(*this); }
  std::string label() const override { return "Program"; }
  std::vector<Child> children() const override {
    std::vector<Child> c;
    add_children(c, "", structs);
    add_children(c, "", functions);
    add_children(c, "", statements);
    c.push_back({"", exit.get()});
    return c;
  }
};
