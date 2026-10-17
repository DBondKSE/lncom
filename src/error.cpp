#include "error.h"

#include <cstdlib>
#include <iostream>

void error(uint64_t line, uint64_t col, const std::string &msg) {
  std::cerr << "compilation error: line " << line << ":" << col << ": " << msg
            << "\n";
  std::exit(1);
}
