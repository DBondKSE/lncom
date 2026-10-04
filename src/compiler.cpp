#include "lexer.h"
#include "parser.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  std::string mode = argc == 3 ? argv[1] : "";
  if (mode != "--tokens" && mode != "--ast") {
    std::cerr << "usage: " << argv[0] << " --ast <source>\n"
              << "       " << argv[0] << " --tokens <source>\n";
    return 1;
  }

  std::ifstream in(argv[2], std::ios::binary);
  if (!in) {
    std::cerr << "error: cannot open " << argv[2] << "\n";
    return 1;
  }
  std::ostringstream src;
  src << in.rdbuf();

  std::vector<token> toks = tokenize(src.str());
  if (mode == "--tokens") {
    print_tokens(toks);
    return 0;
  }
  std::unique_ptr<ProgramNode> program = Parser(std::move(toks)).parse_program();
  program->dump();
  return 0;
}
