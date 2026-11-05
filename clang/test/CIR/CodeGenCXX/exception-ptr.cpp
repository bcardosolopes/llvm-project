// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -fcxx-exceptions -fexceptions
// REQUIRES: system-headers
//
// std::make_exception_ptr - requires system headers not available in lit.
// Previously crashed, now compiles with system headers but lit uses -nostdsysteminc.

#include <exception>
#include <stdexcept>

void test() {
  std::exception_ptr ep = std::make_exception_ptr(std::runtime_error("test"));
}
