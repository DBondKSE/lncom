#include "lexer.h"
#include "error.h"

#include <cstdio>
#include <iostream>

enum lex_state { START, KEY, IDENT, NUMBER, EQUAL, BANG, SLASH, COMMENT };

static const int KEY_BITS = 8;
static const std::string I64_MAX = "9223372036854775807";

std::string key_name(int key) {
  switch (key) {
  case KEY_FALSE:
    return "false";
  case KEY_TRUE:
    return "true";
  case KEY_BOOL:
    return "bool";
  case KEY_ELSE:
    return "else";
  case KEY_I32:
    return "i32";
  case KEY_I64:
    return "i64";
  case KEY_BOOL | KEY_MUT:
    return "bool mut";
  case KEY_I32 | KEY_MUT:
    return "i32 mut";
  case KEY_I64 | KEY_MUT:
    return "i64 mut";
  case KEY_WHILE:
    return "while";
  case KEY_IF:
    return "if";
  case KEY_EXIT:
    return "exit";
  }
  return "";
}

bool is_type_key(int key) {
  if (key < 0)
    return false;
  int base = key & ~KEY_MUT;
  return base == KEY_BOOL || base == KEY_I32 || base == KEY_I64;
}

bool is_mut_key(int key) { return is_type_key(key) && (key & KEY_MUT); }

std::string type_name(int key) { return key_name(key & ~KEY_MUT); }

static const char *kind_name(tok_kind k) {
  switch (k) {
  case TK_KEY:
    return "key";
  case TK_IDENT:
    return "identifier";
  case TK_NUMBER:
    return "number";
  case TK_OPERATOR:
    return "operator";
  case TK_BRACE:
    return "brace";
  case TK_PAREN:
    return "paren";
  case TK_NEWLINE:
    return "newline";
  case TK_EOF:
    return "eof";
  }
  return "?";
}

static bool is_bit(unsigned char b) { return b == ':' || b == '|'; }

static bool is_alpha(unsigned char b) {
  return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || b == '_';
}

static bool is_digit(unsigned char b) { return b >= '0' && b <= '9'; }

static std::string show_byte(unsigned char b) {
  if (b >= 32 && b < 127)
    return std::string(1, (char)b);
  char buf[8];
  snprintf(buf, sizeof buf, "\\x%02X", b);
  return buf;
}

static std::string count_bits(size_t n) {
  return std::to_string(n) + (n == 1 ? " bit" : " bits");
}

static bool fits_i64(const std::string &digits) {
  size_t first = digits.find_first_not_of('0');
  if (first == std::string::npos)
    return true;
  std::string value = digits.substr(first);
  if (value.size() != I64_MAX.size())
    return value.size() < I64_MAX.size();
  return value <= I64_MAX;
}

std::vector<token> tokenize(const std::string &s) {
  std::vector<token> toks;
  lex_state state = START;
  size_t i = 0, start = 0;
  uint64_t line = 1, col = 1, start_col = 1;
  int bits = 0, value = 0;

  while (i <= s.size()) {
    bool eof = i == s.size();
    unsigned char b = eof ? 0 : (unsigned char)s[i];

    if (state == START) {
      if (eof)
        break;
      start = i, start_col = col;
      if (b == '\n') {
        if (toks.empty() || toks.back().kind != TK_NEWLINE)
          toks.push_back({TK_NEWLINE, "\n", line, col});
        i++, line++, col = 1;
        continue;
      } else if (b == ' ' || b == '\t') {
      } else if (is_bit(b)) {
        state = KEY, bits = 0, value = 0;
        continue;
      } else if (is_alpha(b)) {
        state = IDENT;
      } else if (is_digit(b)) {
        state = NUMBER;
      } else if (b == '=') {
        state = EQUAL;
      } else if (b == '!') {
        state = BANG;
      } else if (b == '/') {
        state = SLASH;
      } else if (b == '+' || b == '-' || b == '*') {
        toks.push_back({TK_OPERATOR, std::string(1, (char)b), line, col});
      } else if (b == '{' || b == '}') {
        toks.push_back({TK_BRACE, std::string(1, (char)b), line, col});
      } else if (b == '(' || b == ')') {
        toks.push_back({TK_PAREN, std::string(1, (char)b), line, col});
      } else {
        error(line, col, "unexpected byte '" + show_byte(b) + "'");
      }
    } else if (state == KEY) {
      if (!eof && is_bit(b)) {
        if (bits == KEY_BITS) {
          size_t end = i;
          while (end < s.size() && is_bit((unsigned char)s[end]))
            end++;
          error(line, start_col,
                "key '" + s.substr(start, end - start) + "' has " +
                    count_bits(end - start) + ", a key needs exactly 8");
        }
        value = value * 2 + (b == '|');
        bits++;
      } else {
        std::string text = s.substr(start, i - start);
        if (text == ":" && !eof && b == '=')
          error(line, start_col,
                "':=' is not an operator, assignment is written '='");
        if (bits < KEY_BITS)
          error(line, start_col,
                "key '" + text + "' has " + count_bits(bits) +
                    ", a key needs exactly 8");
        if (!eof && (is_alpha(b) || is_digit(b)))
          error(line, col,
                "'" + show_byte(b) + "' directly after key '" + text +
                    "', separate them with a space");
        if (value == KEY_MUT)
          error(line, start_col,
                "key '" + text + "' is the mutable bit without a type");
        if (key_name(value).empty())
          error(line, start_col,
                "unknown key '" + text + "' (" + std::to_string(value) + ")");
        toks.push_back({TK_KEY, text, line, start_col, value});
        state = START;
        continue;
      }
    } else if (state == IDENT) {
      if (!eof && (is_alpha(b) || is_digit(b))) {
      } else {
        toks.push_back({TK_IDENT, s.substr(start, i - start), line, start_col});
        state = START;
        continue;
      }
    } else if (state == NUMBER) {
      if (!eof && is_digit(b)) {
      } else if (!eof && is_alpha(b)) {
        error(line, start_col,
              "letter '" + show_byte(b) + "' inside number '" +
                  s.substr(start, i - start) + "'");
      } else {
        std::string text = s.substr(start, i - start);
        if (!fits_i64(text))
          error(line, start_col,
                "number " + text + " does not fit in i64 (max " + I64_MAX +
                    ")");
        toks.push_back({TK_NUMBER, text, line, start_col});
        state = START;
        continue;
      }
    } else if (state == EQUAL) {
      state = START;
      if (!eof && b == '=') {
        toks.push_back({TK_OPERATOR, "==", line, start_col});
      } else {
        toks.push_back({TK_OPERATOR, "=", line, start_col});
        continue;
      }
    } else if (state == BANG) {
      state = START;
      if (!eof && b == '=') {
        toks.push_back({TK_OPERATOR, "!=", line, start_col});
      } else {
        toks.push_back({TK_OPERATOR, "!", line, start_col});
        continue;
      }
    } else if (state == SLASH) {
      if (!eof && b == '/')
        state = COMMENT;
      else
        error(line, start_col,
              "a single '/' is not an operator, comments start with '//'");
    } else if (state == COMMENT) {
      if (eof || b == '\n') {
        state = START;
        continue;
      }
    }
    i++;
    col++;
  }
  toks.push_back({TK_EOF, "", line, col});
  return toks;
}

void print_tokens(const std::vector<token> &toks) {
  for (const token &t : toks) {
    std::cout << t.line << ":" << t.col << " " << kind_name(t.kind);
    if (t.kind == TK_KEY)
      std::cout << " " << t.text << " " << key_name(t.key) << " (" << t.key
                << ")";
    else if (t.kind != TK_NEWLINE && t.kind != TK_EOF)
      std::cout << " " << t.text;
    std::cout << "\n";
  }
}

std::string describe(const token &tok) {
  switch (tok.kind) {
  case TK_NEWLINE:
    return "the end of the line";
  case TK_EOF:
    return "the end of the file";
  case TK_KEY:
    return "'" + tok.text + "' (" + key_name(tok.key) + ")";
  default:
    return "'" + tok.text + "'";
  }
}

bool is(const token &tok, tok_kind kind, const std::string &text) {
  return tok.kind == kind && tok.text == text;
}

bool is_key(const token &tok, int key) {
  return tok.kind == TK_KEY && tok.key == key;
}
