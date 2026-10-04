#pragma once

#include "ast.h"
#include "lexer.h"

#include <memory>
#include <string>
#include <vector>

class Parser {
  std::vector<token> toks;
  size_t pos = 0;

  const token &peek() const;
  const token &eat();
  [[noreturn]] void fail(const std::string &msg) const;
  const token &expect(tok_kind kind, const std::string &text,
                      const std::string &what);
  void expect_newline(const std::string &after);

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
  std::unique_ptr<StmtNode> parse_assign();
  const token &parse_type();
  std::unique_ptr<StmtNode> parse_decl();
  std::unique_ptr<StmtNode> parse_statement();

public:
  explicit Parser(std::vector<token> toks);
  std::unique_ptr<ProgramNode> parse_program();
};
