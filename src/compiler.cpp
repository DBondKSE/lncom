#include "codegen.h"
#include "lexer.h"
#include "optimizer.h"
#include "parser.h"
#include "semantic.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static int usage(const char *self) {
  std::cerr << "usage: " << self << " [--optimize] <source> <output.ll>\n"
            << "       " << self << " --ast [--optimize] <source>\n"
            << "       " << self << " --tokens <source>\n"
            << "       " << self << " --check <source>\n"
            << "flags may stand anywhere on the command line\n";
  return 1;
}

int main(int argc, char **argv) {
  std::string mode;
  bool optimized = false;
  std::vector<const char *> paths;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--optimize")
      optimized = true;
    else if (arg == "--ast" || arg == "--tokens" || arg == "--check") {
      if (!mode.empty())
        return usage(argv[0]);
      mode = arg;
    } else if (arg.rfind("--", 0) == 0)
      return usage(argv[0]);
    else
      paths.push_back(argv[i]);
  }
  if (paths.size() != (mode.empty() ? 2u : 1u))
    return usage(argv[0]);
  const char *source = paths[0];

  std::ifstream in(source, std::ios::binary);
  if (!in) {
    std::cerr << "error: cannot open " << source << "\n";
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
  if (mode == "--ast" && !optimized) {
    program->dump();
    return 0;
  }
  SemanticChecker checker;
  program->accept(checker);
  if (mode == "--check")
    return 0;
  if (optimized)
    optimize(*program);
  if (mode == "--ast") {
    program->dump();
    return 0;
  }
  return compile(*program, paths[1]);
}
