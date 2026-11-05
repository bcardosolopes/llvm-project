// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that noreturn attribute is propagated to call sites, matching OGCG.

__attribute__((noreturn)) void die(void);

void might_die(int x) {
  if (x) die();
}

// CHECK: call void @die() #[[NORET:[0-9]+]]
// CHECK: unreachable
// CHECK: attributes #[[NORET]] = { noreturn }
