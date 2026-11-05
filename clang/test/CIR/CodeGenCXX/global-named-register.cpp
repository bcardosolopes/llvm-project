// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
//
// Test: global named register variable access (previously NYI, now implemented).
register unsigned long sp asm("rsp");

unsigned long get_sp() {
  return sp;
}

// CHECK: cir.read_register "rsp" : !u64i
