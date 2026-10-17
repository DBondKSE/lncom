#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum tok_kind {
  TK_KEY,
  TK_IDENT,
  TK_NUMBER,
  TK_OPERATOR,
  TK_BRACE,
  TK_PAREN,
  TK_COMMA,
  TK_DOT,
  TK_STRING,
  TK_NEWLINE,
  TK_EOF
};

enum key_value {
  KEY_FALSE = 0b00000000,
  KEY_TRUE = 0b00000001,
  KEY_BOOL = 0b00000010,
  KEY_STRUCT = 0b00000110,
  KEY_ELSE = 0b00001111,
  KEY_I32 = 0b00100000,
  KEY_I64 = 0b01000000,
  KEY_FN = 0b01100000,
  KEY_READ = 0b01100001,
  KEY_PRINT = 0b01100010,
  KEY_MUT = 0b10000000,
  KEY_WHILE = 0b10101010,
  KEY_IF = 0b11110000,
  KEY_EXIT = 0b11111111
};

struct token {
  tok_kind kind;
  std::string text;
  uint64_t line;
  uint64_t col;
  int key = -1;
};

std::vector<token> tokenize(const std::string &s);
void print_tokens(const std::vector<token> &toks);

std::string key_name(int key);
bool is_type_key(int key);
bool is_mut_key(int key);
bool is_struct_key(int key);
std::string type_name(int key);

std::string describe(const token &tok);
bool is(const token &tok, tok_kind kind, const std::string &text);
bool is_key(const token &tok, int key);
