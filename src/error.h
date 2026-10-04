#pragma once

#include <cstdint>
#include <string>

[[noreturn]] void error(uint64_t line, uint64_t col, const std::string &msg);
