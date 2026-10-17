#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static int usage(const char *self) {
  std::cerr << "usage: " << self << " <source> <output.ll>\n"
            << "       " << self << " --ast <source>\n"
            << "       " << self << " --tokens <source>\n"
            << "       " << self << " --check <source>\n";
  return 1;
}

int main(int argc, char **argv) {
  if (argc != 3)
    return usage(argv[0]);
  std::string first = argv[1];
  bool flag = first.rfind("--", 0) == 0;
  if (flag && first != "--tokens" && first != "--ast" && first != "--check")
    return usage(argv[0]);
  const char *source = flag ? argv[2] : argv[1];

  std::ifstream in(source, std::ios::binary);
  if (!in) {
    std::cerr << "error: cannot open " << source << "\n";
    return 1;
  }
  std::ostringstream src;
  src << in.rdbuf();

  std::vector<token> toks = tokenize(src.str());
  if (first == "--tokens") {
    print_tokens(toks);
    return 0;
  }
  std::unique_ptr<ProgramNode> program = Parser(std::move(toks)).parse_program();
  if (first == "--ast") {
    program->dump();
    return 0;
  }
  SemanticChecker checker;
  program->accept(checker);
  if (first == "--check")
    return 0;
  return compile(*program, argv[2]);
}
