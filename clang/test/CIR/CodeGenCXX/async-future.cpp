// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -fcxx-exceptions -fexceptions
// REQUIRES: system-headers
//
// std::async/std::future requires system headers not available in lit
// (-nostdsysteminc).
// Previously crashed in direct-CIR exception handling; now keep it as a
// system-header compile smoke test.

#include <future>

void test() {
  auto f = std::async(std::launch::async, []{ return 42; });
  int result = f.get();
}
