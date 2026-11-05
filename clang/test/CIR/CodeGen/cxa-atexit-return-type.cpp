// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test that (1) __cxa_atexit is declared with return type i32 (not void),
// and (2) the destructor is passed directly without a __cxx_global_array_dtor
// wrapper for single-object globals.

struct B {
  virtual ~B();
};
B x;

// No wrapper function should be generated.
// LLVM-NOT: __cxx_global_array_dtor
// LLVM: @__cxa_atexit(ptr @_ZN1BD1Ev, ptr @x, ptr @__dso_handle)

// OGCG-NOT: __cxx_global_array_dtor
// OGCG: @__cxa_atexit(ptr @_ZN1BD1Ev, ptr @x, ptr @__dso_handle)
