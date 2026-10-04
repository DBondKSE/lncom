#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class ProgramNode;
class BlockNode;
class ExitNode;
class DeclNode;
class AssignNode;
class IfNode;
class WhileNode;
class BinOpNode;
class NotNode;
class VarNode;
class IntNode;
class BoolNode;

class Visitor {
public:
  virtual ~Visitor() = default;
  virtual void visit_program(ProgramNode &node) = 0;
  virtual void visit_block(BlockNode &node) = 0;
  virtual void visit_exit(ExitNode &node) = 0;
  virtual void visit_decl(DeclNode &node) = 0;
  virtual void visit_assign(AssignNode &node) = 0;
  virtual void visit_if(IfNode &node) = 0;
  virtual void visit_while(WhileNode &node) = 0;
  virtual void visit_binop(BinOpNode &node) = 0;
  virtual void visit_not(NotNode &node) = 0;
  virtual void visit_var(VarNode &node) = 0;
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

class ExprNode : public ASTNode {
public:
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
  VarNode(uint64_t line, uint64_t col, std::string name)
      : ExprNode(line, col), name(std::move(name)) {}
  void accept(Visitor &visitor) override { visitor.visit_var(*this); }
  std::string label() const override { return "Var " + name; }
};

class IntNode : public ExprNode {
public:
  std::string text;
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
  std::string name;
  bool mut;
  std::unique_ptr<ExprNode> init;
  DeclNode(uint64_t line, uint64_t col, std::string type_name,
           std::string name, bool mut, std::unique_ptr<ExprNode> init)
      : StmtNode(line, col), type_name(std::move(type_name)),
        name(std::move(name)), mut(mut), init(std::move(init)) {}
  void accept(Visitor &visitor) override { visitor.visit_decl(*this); }
  std::string label() const override {
    return "Decl " + name + " " + type_name + (mut ? " mut" : " const");
  }
  std::vector<Child> children() const override {
    return {{"init", init.get()}};
  }
};

class AssignNode : public StmtNode {
public:
  std::string name;
  std::unique_ptr<ExprNode> value;
  AssignNode(uint64_t line, uint64_t col, std::string name,
             std::unique_ptr<ExprNode> value)
      : StmtNode(line, col), name(std::move(name)), value(std::move(value)) {}
  void accept(Visitor &visitor) override { visitor.visit_assign(*this); }
  std::string label() const override { return "Assign " + name; }
  std::vector<Child> children() const override {
    return {{"value", value.get()}};
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
    return {{"value", value.get()}};
  }
};

class BlockNode : public ASTNode {
public:
  std::vector<std::unique_ptr<StmtNode>> statements;
  std::unique_ptr<ExitNode> exit;
  BlockNode(uint64_t line, uint64_t col,
            std::vector<std::unique_ptr<StmtNode>> statements,
            std::unique_ptr<ExitNode> exit)
      : ASTNode(line, col), statements(std::move(statements)),
        exit(std::move(exit)) {}
  void accept(Visitor &visitor) override { visitor.visit_block(*this); }
  std::string label() const override { return "Block"; }
  std::vector<Child> children() const override {
    std::vector<Child> c;
    for (const std::unique_ptr<StmtNode> &s : statements)
      c.push_back({"", s.get()});
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

class ProgramNode : public ASTNode {
public:
  std::vector<std::unique_ptr<StmtNode>> statements;
  std::unique_ptr<ExitNode> exit;
  ProgramNode(uint64_t line, uint64_t col,
              std::vector<std::unique_ptr<StmtNode>> statements,
              std::unique_ptr<ExitNode> exit)
      : ASTNode(line, col), statements(std::move(statements)),
        exit(std::move(exit)) {}
  void accept(Visitor &visitor) override { visitor.visit_program(*this); }
  std::string label() const override { return "Program"; }
  std::vector<Child> children() const override {
    std::vector<Child> c;
    for (const std::unique_ptr<StmtNode> &s : statements)
      c.push_back({"", s.get()});
    c.push_back({"", exit.get()});
    return c;
  }
};
