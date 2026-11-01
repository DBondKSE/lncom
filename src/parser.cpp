#include "parser.h"
#include "error.h"

Parser::Parser(std::vector<token> toks) : toks(std::move(toks)) {}

const token &Parser::peek(size_t ahead) const {
  size_t at = pos + ahead;
  return at < toks.size() ? toks[at] : toks.back();
}

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

static std::string at(const token &tok) {
  return std::to_string(tok.line) + ":" + std::to_string(tok.col);
}

bool Parser::at_struct_decl() const {
  return peek().kind == TK_KEY && is_struct_key(peek().key) &&
         peek(1).kind == TK_IDENT && peek(2).kind == TK_NEWLINE;
}

std::vector<std::unique_ptr<ExprNode>> Parser::parse_args() {
  const token &open = eat();
  std::vector<std::unique_ptr<ExprNode>> args;
  if (is(peek(), TK_PAREN, ")")) {
    eat();
    return args;
  }
  while (true) {
    args.push_back(parse_expr());
    if (peek().kind != TK_COMMA)
      break;
    eat();
  }
  expect(TK_PAREN, ")", "')' to close the '(' at " + at(open));
  return args;
}

std::unique_ptr<ExprNode> Parser::parse_chain() {
  const token &name = eat();
  std::unique_ptr<ExprNode> node;
  if (is(peek(), TK_PAREN, "("))
    node = std::make_unique<CallNode>(name.line, name.col, name.text,
                                      parse_args());
  else
    node = std::make_unique<VarNode>(name.line, name.col, name.text);
  while (peek().kind == TK_DOT) {
    const token &dot = eat();
    const token &member =
        expect(TK_IDENT, "", "a field or member function name after '.'");
    if (is(peek(), TK_PAREN, "(")) {
      node = std::make_unique<MemberCallNode>(member.line, member.col,
                                              std::move(node), dot.col,
                                              member.text, parse_args());
      continue;
    }
    Link link{member.text, member.col};
    if (VarNode *var = dynamic_cast<VarNode *>(node.get()))
      var->links.push_back(link);
    else if (FieldOfNode *field = dynamic_cast<FieldOfNode *>(node.get()))
      field->links.push_back(link);
    else
      node = std::make_unique<FieldOfNode>(dot.line, dot.col, std::move(node),
                                           std::vector<Link>{link});
  }
  if (peek().kind == TK_AT) {
    const token &at = eat();
    node = std::make_unique<IndexNode>(at.line, at.col, std::move(node),
                                       parse_index());
  }
  return node;
}

std::unique_ptr<ExprNode> Parser::parse_index() {
  const token &tok = peek();
  if (tok.kind == TK_NUMBER) {
    eat();
    return std::make_unique<IntNode>(tok.line, tok.col, tok.text);
  }
  if (tok.kind == TK_IDENT)
    return parse_chain();
  if (is(tok, TK_PAREN, "(")) {
    eat();
    std::unique_ptr<ExprNode> inner = parse_expr();
    expect(TK_PAREN, ")", "')' to close the '(' at " + at(tok));
    return inner;
  }
  fail("expected a number, a variable or '(' as the index after '@'");
}

std::string Parser::parse_array_shape(const token &element) {
  if (is_mut_key(element.key))
    error(element.line, element.col,
          "the elements of an array take a plain type key, mutability "
          "belongs to the array key");
  if (!is_builtin_key(element.key))
    error(element.line, element.col,
          "an array holds i32, i64 or bool, not " + describe(element));
  expect(TK_AT, "", "'@' and the size after the element type");
  const token &size =
      expect(TK_NUMBER, "", "the size of the array after '@'");
  uint64_t n = size.text.size() > 5 ? 0 : std::stoull(size.text);
  if (n < 1 || n > 4096)
    error(size.line, size.col,
          "an array holds 1 to 4096 elements, got " + size.text);
  return array_type(type_name(element.key), n);
}

std::unique_ptr<ExprNode> Parser::parse_factor() {
  const token &tok = peek();
  if (is(tok, TK_OPERATOR, "!")) {
    eat();
    return std::make_unique<NotNode>(tok.line, tok.col, parse_factor());
  }
  if (tok.kind == TK_AT) {
    eat();
    return std::make_unique<ThreadIndexNode>(tok.line, tok.col);
  }
  if (tok.kind == TK_KEY && peek(1).kind == TK_AT && !is_array_key(tok.key)) {
    eat();
    std::string type = parse_array_shape(tok);
    std::unique_ptr<ExprNode> fill;
    if (peek().kind == TK_AT) {
      eat();
      fill = parse_factor();
    }
    return std::make_unique<ArrayNode>(tok.line, tok.col, element_type(type),
                                       array_size(type), std::move(fill));
  }
  if (tok.kind == TK_NUMBER) {
    eat();
    return std::make_unique<IntNode>(tok.line, tok.col, tok.text);
  }
  if (is_key(tok, KEY_TRUE) || is_key(tok, KEY_FALSE)) {
    eat();
    return std::make_unique<BoolNode>(tok.line, tok.col, tok.key == KEY_TRUE);
  }
  if (is_key(tok, KEY_READ)) {
    eat();
    return std::make_unique<ReadNode>(tok.line, tok.col);
  }
  if (tok.kind == TK_IDENT)
    return parse_chain();
  if (is(tok, TK_PAREN, "(")) {
    eat();
    std::unique_ptr<ExprNode> inner = parse_expr();
    expect(TK_PAREN, ")", "')' to close the '(' at " + at(tok));
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
  if (peek().kind == TK_NEWLINE || peek().kind == TK_EOF)
    return std::make_unique<ExitNode>(kw.line, kw.col, nullptr);
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
  const token &close = eat();
  if (peek().kind != TK_EOF)
    expect_newline("'}'");
  return std::make_unique<BlockNode>(brace.line, brace.col,
                                     std::move(statements), std::move(exit),
                                     close.line, close.col);
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

std::unique_ptr<StmtNode> Parser::parse_print() {
  const token &kw = eat();
  if (peek().kind == TK_STRING)
    return std::make_unique<PrintNode>(kw.line, kw.col, nullptr, eat().text);
  return std::make_unique<PrintNode>(kw.line, kw.col, parse_expr(), "");
}

std::unique_ptr<StmtNode> Parser::parse_chain_stmt() {
  const token &first = peek();
  std::unique_ptr<ExprNode> target = parse_chain();
  IndexNode *element = dynamic_cast<IndexNode *>(target.get());
  VarNode *var = dynamic_cast<VarNode *>(element ? element->array.get()
                                                 : target.get());
  if (is(peek(), TK_OPERATOR, "=")) {
    if (!var)
      error(first.line, first.col,
            "cannot assign to a call result, only to a variable or its fields");
    eat();
    std::unique_ptr<ExprNode> value = parse_expr();
    if (element)
      return std::make_unique<AssignNode>(
          first.line, first.col, var->name, var->links, std::move(value),
          std::move(element->index), element->col);
    return std::make_unique<AssignNode>(first.line, first.col, var->name,
                                        var->links, std::move(value));
  }
  if (dynamic_cast<CallNode *>(target.get()) ||
      dynamic_cast<MemberCallNode *>(target.get()))
    return std::make_unique<CallStmtNode>(first.line, first.col,
                                          std::move(target));
  if (var)
    fail("expected '=' after '" + chain(var->name, var->links) +
         (element ? "@...'" : "'"));
  fail("expected '=' or a call, a field of a call result cannot stand alone");
}

parsed_type Parser::parse_type(const std::string &expected) {
  const token &key = peek();
  if (key.kind != TK_KEY || !is_type_key(key.key))
    fail("expected " + expected);
  eat();
  parsed_type t{&key, type_name(key.key), key.col, false, is_mut_key(key.key),
                describe(key)};
  if (is_struct_key(key.key)) {
    const token &name =
        expect(TK_IDENT, "", "a struct name after " + describe(key));
    t.name = name.text;
    t.col = name.col;
    t.is_struct = true;
    t.what += " '" + name.text + "'";
  }
  if (is_array_key(key.key) && peek().kind == TK_AT) {
    eat();
    const token &element = peek();
    if (element.kind != TK_KEY)
      fail("expected the element type of the array after '@'");
    eat();
    t.name = parse_array_shape(element);
    t.what += " " + t.name;
  }
  return t;
}

void Parser::require_array_shape(const parsed_type &type,
                                 const std::string &what) {
  if (type.name == "array")
    error(type.key->line, type.key->col,
          "the type of " + what +
              " is written as the array key, '@', the element type, '@' and "
              "the size");
}

std::unique_ptr<InitNode> Parser::parse_init(const token &open,
                                             const std::string &close) {
  std::vector<std::unique_ptr<ExprNode>> values;
  while (true) {
    values.push_back(parse_value());
    if (peek().kind != TK_COMMA)
      break;
    eat();
  }
  expect(TK_BRACE, "}", close);
  return std::make_unique<InitNode>(open.line, open.col, std::move(values));
}

std::unique_ptr<ExprNode> Parser::parse_value() {
  if (!is(peek(), TK_BRACE, "{"))
    return parse_expr();
  const token &open = eat();
  return parse_init(open, "'}' to close the '{' at " + at(open));
}

std::unique_ptr<StmtNode> Parser::parse_decl() {
  parsed_type type = parse_type("a type key");
  if (is_array_type(type.name))
    error(type.key->line, type.key->col,
          "an array variable takes its element type and size from its "
          "initial value, write the array key alone");
  const token &name =
      expect(TK_IDENT, "", "a variable name after " + type.what);
  const token &open = expect(TK_BRACE, "{",
                             "'{' with the initial value of '" + name.text +
                                 "'");
  std::unique_ptr<InitNode> init = parse_init(
      open, "'}' after the initial value of '" + name.text + "'");
  return std::make_unique<DeclNode>(type.key->line, type.key->col, type.name,
                                    type.is_struct, type.col, name.text,
                                    type.mut, std::move(init));
}

std::unique_ptr<StmtNode> Parser::parse_statement() {
  const token &tok = peek();
  if (at_struct_decl()) {
    if (is_mut_key(tok.key))
      error(tok.line, tok.col,
            "a struct is declared with the plain struct key, mutability "
            "belongs to its fields and objects");
    error(tok.line, tok.col,
          "a struct is declared at the top level, before the functions and "
          "the statements");
  }
  if (is_key(tok, KEY_FN))
    error(tok.line, tok.col,
          "a function is declared at the top level or in a struct, before the "
          "statements");
  if (is_type_key(tok.key)) {
    std::unique_ptr<StmtNode> decl = parse_decl();
    expect_newline("the declaration of a variable");
    return decl;
  }
  if (tok.kind == TK_IDENT) {
    std::unique_ptr<StmtNode> stmt = parse_chain_stmt();
    if (dynamic_cast<CallStmtNode *>(stmt.get()))
      expect_newline("the call");
    else
      expect_newline("the assignment to '" + tok.text + "'");
    return stmt;
  }
  if (is_key(tok, KEY_PRINT)) {
    std::unique_ptr<StmtNode> print = parse_print();
    expect_newline("print");
    return print;
  }
  if (is_key(tok, KEY_IF))
    return parse_if();
  if (is_key(tok, KEY_WHILE))
    return parse_while();
  if (is_key(tok, KEY_SPAWN))
    return parse_spawn();
  if (is_key(tok, KEY_LOCK))
    return parse_lock();
  if (is_key(tok, KEY_MUTEX))
    error(tok.line, tok.col,
          "a mutex is declared at the top level, after the structs and "
          "before the functions");
  if (tok.kind == TK_AT)
    error(tok.line, tok.col, "a range line stands only in a spawn block");
  if (is_key(tok, KEY_ELSE))
    error(tok.line, tok.col, describe(tok) + " without an if");
  if (is(tok, TK_BRACE, "}"))
    error(tok.line, tok.col, "'}' without a matching '{'");
  if (is(tok, TK_BRACE, "{"))
    error(tok.line, tok.col, "a block can only follow if, else or while");
  error(tok.line, tok.col, "cannot start a statement with " + describe(tok));
}

std::unique_ptr<StmtNode> Parser::parse_spawn_line() {
  const token &tok = peek();
  if (tok.kind == TK_AT) {
    eat();
    std::unique_ptr<ExprNode> count = parse_factor();
    const token &name = peek();
    if (name.kind != TK_IDENT)
      fail("expected a function call after the thread count of the range "
           "line");
    std::unique_ptr<ExprNode> target = parse_chain();
    if (!dynamic_cast<CallNode *>(target.get()))
      error(name.line, name.col,
            "a range line calls a function of the program");
    std::unique_ptr<CallNode> call(static_cast<CallNode *>(target.release()));
    expect_newline("the range line");
    return std::make_unique<RangeNode>(tok.line, tok.col, std::move(count),
                                       std::move(call));
  }
  if (is_type_key(tok.key)) {
    std::unique_ptr<StmtNode> stmt = parse_decl();
    InitNode &init = *static_cast<DeclNode &>(*stmt).init;
    if (init.values.size() != 1 ||
        !dynamic_cast<CallNode *>(init.values[0].get()))
      error(init.line, init.col,
            "a declaration in a spawn block takes the result of one function "
            "call as its value");
    expect_newline("the declaration of a variable");
    return stmt;
  }
  if (tok.kind == TK_IDENT) {
    std::unique_ptr<StmtNode> stmt = parse_chain_stmt();
    CallStmtNode *call = dynamic_cast<CallStmtNode *>(stmt.get());
    if (!call || !dynamic_cast<CallNode *>(call->call.get()))
      error(tok.line, tok.col,
            "a spawn line is a function call, a declaration from one, or a "
            "range line");
    expect_newline("the call");
    return stmt;
  }
  if (is(tok, TK_BRACE, "{"))
    error(tok.line, tok.col, "a block cannot stand in a spawn block");
  error(tok.line, tok.col,
        "a spawn line is a function call, a declaration from one, or a range "
        "line, not " +
            describe(tok));
}

std::unique_ptr<StmtNode> Parser::parse_spawn() {
  const token &kw = eat();
  expect_newline(describe(kw));
  if (!is(peek(), TK_BRACE, "{"))
    fail("expected '{' on the line after " + describe(kw));
  const token &brace = eat();
  expect_newline("'{'");
  std::vector<std::unique_ptr<StmtNode>> lines;
  while (!is(peek(), TK_BRACE, "}")) {
    if (peek().kind == TK_EOF)
      error(brace.line, brace.col, "'{' is never closed");
    lines.push_back(parse_spawn_line());
  }
  if (lines.empty())
    error(brace.line, brace.col,
          "empty spawn block, it needs at least one call");
  eat();
  if (peek().kind != TK_EOF)
    expect_newline("'}'");
  return std::make_unique<SpawnNode>(kw.line, kw.col, std::move(lines));
}

std::unique_ptr<StmtNode> Parser::parse_lock() {
  const token &kw = eat();
  const token &name =
      expect(TK_IDENT, "", "a mutex name after " + describe(kw));
  expect_newline("the name of mutex '" + name.text + "'");
  std::unique_ptr<BlockNode> body = parse_block(kw);
  return std::make_unique<LockNode>(kw.line, kw.col, name.text, name.col,
                                    std::move(body));
}

std::unique_ptr<MutexNode> Parser::parse_mutex_decl() {
  const token &kw = eat();
  const token &name =
      expect(TK_IDENT, "", "a mutex name after " + describe(kw));
  expect_newline("the mutex '" + name.text + "'");
  return std::make_unique<MutexNode>(kw.line, kw.col, name.text);
}

std::unique_ptr<ParamNode> Parser::parse_param() {
  parsed_type type = parse_type("the type of a parameter");
  if (type.mut)
    error(type.key->line, type.key->col,
          "a parameter is a constant, its type takes a plain type key, not " +
              describe(*type.key));
  require_array_shape(type, "an array parameter");
  const token &name =
      expect(TK_IDENT, "", "a parameter name after " + type.what);
  return std::make_unique<ParamNode>(type.key->line, type.key->col, type.name,
                                     type.is_struct, type.col, name.text);
}

std::vector<std::unique_ptr<ParamNode>>
Parser::parse_params(const token &name) {
  const token &open =
      expect(TK_PAREN, "(", "'(' with the parameters of '" + name.text + "'");
  std::vector<std::unique_ptr<ParamNode>> params;
  if (is(peek(), TK_PAREN, ")")) {
    eat();
    return params;
  }
  while (true) {
    params.push_back(parse_param());
    if (peek().kind != TK_COMMA)
      break;
    eat();
  }
  expect(TK_PAREN, ")", "')' to close the '(' at " + at(open));
  return params;
}

template <class T> std::unique_ptr<T> Parser::parse_fn_decl() {
  const token &kw = eat();
  const token &name =
      expect(TK_IDENT, "", "a function name after " + describe(kw));
  std::vector<std::unique_ptr<ParamNode>> params = parse_params(name);
  const token &result = peek();
  std::string result_type;
  bool result_struct = false;
  if (is_key(result, KEY_FALSE)) {
    eat();
  } else {
    if (is_mut_key(result.key))
      error(result.line, result.col,
            "a result type takes a plain type key, not " + describe(result));
    parsed_type type = parse_type("the result type of '" + name.text +
                                  "' after its parameters, or '::::::::' "
                                  "for none");
    require_array_shape(type, "an array result");
    result_type = type.name;
    result_struct = type.is_struct;
  }
  expect_newline("the header of function '" + name.text + "'");
  std::unique_ptr<BlockNode> body = parse_block(kw);
  if (!result_type.empty() && !body->exit)
    error(body->end_line, body->end_col,
          "function '" + name.text + "' returns " +
              type_label(result_type, result_struct) +
              ", its body must end with exit");
  return std::make_unique<T>(kw.line, kw.col, name.text, std::move(params),
                             result_type, result_struct, result.col,
                             std::move(body));
}

std::unique_ptr<FieldNode> Parser::parse_field() {
  parsed_type type = parse_type("a field");
  require_array_shape(type, "an array field");
  const token &name = expect(TK_IDENT, "", "a field name after " + type.what);
  expect_newline("the field '" + name.text + "'");
  return std::make_unique<FieldNode>(type.key->line, type.key->col, type.name,
                                     type.is_struct, type.col, name.text,
                                     type.mut);
}

std::unique_ptr<StructNode> Parser::parse_struct_decl() {
  const token &kw = eat();
  if (is_mut_key(kw.key))
    error(kw.line, kw.col,
          "a struct is declared with the plain struct key, mutability "
          "belongs to its fields and objects");
  const token &name = eat();
  std::string what = "struct '" + name.text + "'";
  expect_newline("the name of " + what);
  if (!is(peek(), TK_BRACE, "{"))
    fail("expected '{' on the line after " + what);
  const token &brace = eat();
  expect_newline("'{'");
  std::vector<std::unique_ptr<FieldNode>> fields;
  if (!is_type_key(peek().key))
    fail("expected a field, " + what + " needs at least one field");
  while (is_type_key(peek().key))
    fields.push_back(parse_field());
  std::vector<std::unique_ptr<MemberFnNode>> methods;
  while (is_key(peek(), KEY_FN))
    methods.push_back(parse_fn_decl<MemberFnNode>());
  const token &tok = peek();
  if (tok.kind == TK_EOF)
    error(brace.line, brace.col, "'{' is never closed");
  if (is_type_key(tok.key))
    error(tok.line, tok.col,
          "the fields of " + what + " come before its member functions");
  if (!is(tok, TK_BRACE, "}"))
    fail("expected a field, a member function or '}' in " + what);
  eat();
  if (peek().kind != TK_EOF)
    expect_newline("'}'");
  return std::make_unique<StructNode>(kw.line, kw.col, name.text,
                                      std::move(fields), std::move(methods));
}

std::unique_ptr<ProgramNode> Parser::parse_program() {
  if (peek().kind == TK_NEWLINE)
    eat();
  std::vector<std::unique_ptr<StructNode>> structs;
  while (at_struct_decl())
    structs.push_back(parse_struct_decl());
  std::vector<std::unique_ptr<MutexNode>> mutexes;
  while (is_key(peek(), KEY_MUTEX))
    mutexes.push_back(parse_mutex_decl());
  std::vector<std::unique_ptr<FnNode>> functions;
  while (is_key(peek(), KEY_FN))
    functions.push_back(parse_fn_decl<FnNode>());
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
  return std::make_unique<ProgramNode>(1, 1, std::move(structs),
                                       std::move(mutexes), std::move(functions),
                                       std::move(statements), std::move(exit));
}
