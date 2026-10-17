#include "parser.h"
#include "error.h"

Parser::Parser(std::vector<token> toks) : toks(std::move(toks)) {}

const token &Parser::peek() const { return toks[pos]; }

const token &Parser::eat() {
  const token &tok = toks[pos];
  if (tok.kind != TK_EOF)
    pos++;
  return tok;
}

void Parser::fail(const std::string &msg) const {
  const token &tok = peek();
  error(tok.line, tok.col, msg + ", got " + describe(tok));
}

const token &Parser::expect(tok_kind kind, const std::string &text,
                            const std::string &what) {
  const token &tok = peek();
  if (tok.kind != kind || (!text.empty() && tok.text != text))
    fail("expected " + what);
  return eat();
}

void Parser::expect_newline(const std::string &after) {
  if (peek().kind != TK_NEWLINE)
    fail("expected the end of the line after " + after);
  eat();
}

std::unique_ptr<ExprNode> Parser::parse_factor() {
  const token &tok = peek();
  if (is(tok, TK_OPERATOR, "!")) {
    eat();
    return std::make_unique<NotNode>(tok.line, tok.col, parse_factor());
  }
  if (tok.kind == TK_NUMBER) {
    eat();
    return std::make_unique<IntNode>(tok.line, tok.col, tok.text);
  }
  if (is_key(tok, KEY_TRUE) || is_key(tok, KEY_FALSE)) {
    eat();
    return std::make_unique<BoolNode>(tok.line, tok.col, tok.key == KEY_TRUE);
  }
  if (tok.kind == TK_IDENT) {
    eat();
    return std::make_unique<VarNode>(tok.line, tok.col, tok.text);
  }
  if (is(tok, TK_PAREN, "(")) {
    eat();
    std::unique_ptr<ExprNode> inner = parse_expr();
    expect(TK_PAREN, ")",
           "')' to close the '(' at " + std::to_string(tok.line) + ":" +
               std::to_string(tok.col));
    return inner;
  }
  fail("expected a number, true, false, a variable, '!' or '('");
}

std::unique_ptr<ExprNode> Parser::parse_term() {
  std::unique_ptr<ExprNode> node = parse_factor();
  while (is(peek(), TK_OPERATOR, "*")) {
    const token &op = eat();
    std::unique_ptr<ExprNode> right = parse_factor();
    node = std::make_unique<BinOpNode>(op.line, op.col, op.text,
                                       std::move(node), std::move(right));
  }
  return node;
}

std::unique_ptr<ExprNode> Parser::parse_arith() {
  std::unique_ptr<ExprNode> node = parse_term();
  while (is(peek(), TK_OPERATOR, "+") || is(peek(), TK_OPERATOR, "-")) {
    const token &op = eat();
    std::unique_ptr<ExprNode> right = parse_term();
    node = std::make_unique<BinOpNode>(op.line, op.col, op.text,
                                       std::move(node), std::move(right));
  }
  return node;
}

static bool is_comparison(const token &tok) {
  return is(tok, TK_OPERATOR, "==") || is(tok, TK_OPERATOR, "!=");
}

std::unique_ptr<ExprNode> Parser::parse_expr() {
  std::unique_ptr<ExprNode> node = parse_arith();
  if (!is_comparison(peek()))
    return node;
  const token &op = eat();
  std::unique_ptr<ExprNode> right = parse_arith();
  node = std::make_unique<BinOpNode>(op.line, op.col, op.text, std::move(node),
                                     std::move(right));
  if (is_comparison(peek()))
    fail("only one comparison is allowed in an expression, use parentheses");
  return node;
}

std::unique_ptr<ExitNode> Parser::parse_exit_stmt() {
  const token &kw = eat();
  return std::make_unique<ExitNode>(kw.line, kw.col, parse_factor());
}

void Parser::parse_body(const token &brace,
                        std::vector<std::unique_ptr<StmtNode>> &statements,
                        std::unique_ptr<ExitNode> &exit) {
  while (true) {
    const token &tok = peek();
    if (tok.kind == TK_EOF)
      error(brace.line, brace.col, "'{' is never closed");
    if (is(tok, TK_BRACE, "}"))
      break;
    if (exit)
      error(tok.line, tok.col, "statement after exit in the same block");
    if (is_key(tok, KEY_EXIT)) {
      exit = parse_exit_stmt();
      expect_newline("exit");
    } else {
      statements.push_back(parse_statement());
    }
  }
  if (statements.empty() && !exit)
    error(brace.line, brace.col,
          "empty block, a block needs at least one statement");
}

std::unique_ptr<BlockNode> Parser::parse_block(const token &owner) {
  if (!is(peek(), TK_BRACE, "{"))
    fail("expected '{' on the line after " + describe(owner));
  const token &brace = eat();
  expect_newline("'{'");
  std::vector<std::unique_ptr<StmtNode>> statements;
  std::unique_ptr<ExitNode> exit;
  parse_body(brace, statements, exit);
  eat();
  if (peek().kind != TK_EOF)
    expect_newline("'}'");
  return std::make_unique<BlockNode>(brace.line, brace.col,
                                     std::move(statements), std::move(exit));
}

std::unique_ptr<StmtNode> Parser::parse_while() {
  const token &kw = eat();
  std::unique_ptr<ExprNode> cond = parse_expr();
  expect_newline("the while condition");
  std::unique_ptr<BlockNode> body = parse_block(kw);
  return std::make_unique<WhileNode>(kw.line, kw.col, std::move(cond),
                                     std::move(body));
}

std::unique_ptr<StmtNode> Parser::parse_if() {
  const token &kw = eat();
  std::unique_ptr<ExprNode> cond = parse_expr();
  expect_newline("the if condition");
  std::unique_ptr<BlockNode> then_block = parse_block(kw);
  std::unique_ptr<BlockNode> else_block;
  if (is_key(peek(), KEY_ELSE)) {
    const token &else_kw = eat();
    expect_newline(describe(else_kw));
    else_block = parse_block(else_kw);
  }
  return std::make_unique<IfNode>(kw.line, kw.col, std::move(cond),
                                  std::move(then_block), std::move(else_block));
}

std::unique_ptr<StmtNode> Parser::parse_assign() {
  const token &name = eat();
  expect(TK_OPERATOR, "=", "'=' after '" + name.text + "'");
  return std::make_unique<AssignNode>(name.line, name.col, name.text,
                                      parse_expr());
}

const token &Parser::parse_type() {
  if (peek().kind != TK_KEY || !is_type_key(peek().key))
    fail("expected a type key");
  return eat();
}

std::unique_ptr<StmtNode> Parser::parse_decl() {
  const token &type = parse_type();
  const token &name =
      expect(TK_IDENT, "", "a variable name after " + describe(type));
  expect(TK_BRACE, "{", "'{' with the initial value of '" + name.text + "'");
  std::unique_ptr<ExprNode> init = parse_expr();
  expect(TK_BRACE, "}", "'}' after the initial value of '" + name.text + "'");
  return std::make_unique<DeclNode>(type.line, type.col, type_name(type.key),
                                    name.text, is_mut_key(type.key),
                                    std::move(init));
}

std::unique_ptr<StmtNode> Parser::parse_statement() {
  const token &tok = peek();
  if (is_type_key(tok.key)) {
    std::unique_ptr<StmtNode> decl = parse_decl();
    expect_newline("the declaration of a variable");
    return decl;
  }
  if (tok.kind == TK_IDENT) {
    std::unique_ptr<StmtNode> assign = parse_assign();
    expect_newline("the assignment to '" + tok.text + "'");
    return assign;
  }
  if (is_key(tok, KEY_IF))
    return parse_if();
  if (is_key(tok, KEY_WHILE))
    return parse_while();
  if (is_key(tok, KEY_ELSE))
    error(tok.line, tok.col, describe(tok) + " without an if");
  if (is(tok, TK_BRACE, "}"))
    error(tok.line, tok.col, "'}' without a matching '{'");
  if (is(tok, TK_BRACE, "{"))
    error(tok.line, tok.col, "a block can only follow if, else or while");
  error(tok.line, tok.col, "cannot start a statement with " + describe(tok));
}

std::unique_ptr<ProgramNode> Parser::parse_program() {
  if (peek().kind == TK_NEWLINE)
    eat();
  std::vector<std::unique_ptr<StmtNode>> statements;
  while (!is_key(peek(), KEY_EXIT)) {
    if (peek().kind == TK_EOF)
      error(peek().line, peek().col, "the program has no exit");
    statements.push_back(parse_statement());
  }
  std::unique_ptr<ExitNode> exit = parse_exit_stmt();
  if (peek().kind == TK_NEWLINE)
    eat();
  if (peek().kind != TK_EOF)
    fail("exit must be the last statement of the program");
  return std::make_unique<ProgramNode>(1, 1, std::move(statements),
                                       std::move(exit));
}
