#pragma once

#include "ast.h"
#include "lexer.h"

#include <memory>
#include <string>
#include <vector>

struct parsed_type {
  const token *key;
  std::string name;
  uint64_t col;
  bool is_struct;
  bool mut;
  std::string what;
};

class Parser {
  std::vector<token> toks;
  size_t pos = 0;

  const token &peek(size_t ahead = 0) const;
  const token &eat();
  [[noreturn]] void fail(const std::string &msg) const;
  const token &expect(tok_kind kind, const std::string &text,
                      const std::string &what);
  void expect_newline(const std::string &after);
  bool at_struct_decl() const;

  std::vector<std::unique_ptr<ExprNode>> parse_args();
  std::unique_ptr<ExprNode> parse_chain();
  std::unique_ptr<ExprNode> parse_factor();
  std::unique_ptr<ExprNode> parse_term();
  std::unique_ptr<ExprNode> parse_arith();
  std::unique_ptr<ExprNode> parse_expr();
  std::unique_ptr<ExitNode> parse_exit_stmt();
  void parse_body(const token &brace,
                  std::vector<std::unique_ptr<StmtNode>> &statements,
                  std::unique_ptr<ExitNode> &exit);
  std::unique_ptr<BlockNode> parse_block(const token &owner);
  std::unique_ptr<StmtNode> parse_while();
  std::unique_ptr<StmtNode> parse_if();
  std::unique_ptr<StmtNode> parse_print();
  std::unique_ptr<StmtNode> parse_chain_stmt();
  parsed_type parse_type(const std::string &expected);
  std::unique_ptr<InitNode> parse_init(const token &open,
                                       const std::string &close);
  std::unique_ptr<ExprNode> parse_value();
  std::unique_ptr<StmtNode> parse_decl();
  std::unique_ptr<StmtNode> parse_statement();
  std::unique_ptr<ParamNode> parse_param();
  std::vector<std::unique_ptr<ParamNode>> parse_params(const token &name);
  template <class T> std::unique_ptr<T> parse_fn_decl();
  std::unique_ptr<FieldNode> parse_field();
  std::unique_ptr<StructNode> parse_struct_decl();

public:
  explicit Parser(std::vector<token> toks);
  std::unique_ptr<ProgramNode> parse_program();
};
