// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s
//
// Previously crashed with assertion failure in IntType::get when inline asm
// output operand had zero size. Now produces clean error diagnostic.

struct empty {};

void test(struct empty bar) {
  // CHECK: error: output size should not be zero
  asm ("" : "=r"(bar));
}
