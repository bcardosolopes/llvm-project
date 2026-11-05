// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Non-trivially-destructible structs must use the sret calling convention
// on x86_64 SysV ABI. The Itanium C++ ABI requires pass-by-address for any
// type that cannot pass in registers (e.g., has a non-trivial destructor).
//
// CIR now emits `dead_on_unwind` and `writable` attributes
// on the sret parameter, matching OGCG (these are optimizer hints, not ABI-required).

struct S {
  int x;
  ~S();  // Non-trivial destructor: cannot pass in registers → sret required
};

S foo() {
  S s;
  s.x = 42;
  return s;
}

// CIR: define dso_local void @_Z3foov(ptr dead_on_unwind noalias writable sret(%struct.S) align 4
// CIR-NOT: define dso_local {{i32|%struct.S}} @_Z3foov

// OGCG: define dso_local void @_Z3foov(ptr dead_on_unwind noalias writable sret(%struct.S) align 4
// OGCG-NOT: define dso_local {{i32|%struct.S}} @_Z3foov
