// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test global variable with virtual method and destructor.
// The destructor should be passed directly to __cxa_atexit without
// a __cxx_global_array_dtor wrapper, matching OGCG behavior.

class a {
public:
  ~a();
  virtual char b();
} c;

// CIR-NOT: __cxx_global_array_dtor
// CIR: cir.func no_inline internal private @__cxx_global_var_init
// CIR:   cir.call @__cxa_atexit

// LLVM-NOT: __cxx_global_array_dtor
// LLVM: define internal void @__cxx_global_var_init()
// LLVM:   call i32 @__cxa_atexit(ptr @_ZN1aD1Ev, ptr @c, ptr @__dso_handle)

// OGCG-NOT: __cxx_global_array_dtor
// OGCG: define internal void @__cxx_global_var_init()
// OGCG:   call i32 @__cxa_atexit(ptr @_ZN1aD1Ev, ptr @c, ptr @__dso_handle)
