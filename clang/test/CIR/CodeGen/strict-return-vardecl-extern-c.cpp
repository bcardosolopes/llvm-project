// Test that indirect calls through extern "C" function pointer variables
// in C++ mode don't get noundef on the return value, matching OGCG's
// HasStrictReturn() behavior for VarDecl extern "C" (CGCall.cpp:1923-1926).

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll -x c++
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll -x c++
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

extern "C" {
  int (*extern_c_fp)(int);
}

int (*cpp_fp)(int);

// Indirect call through extern "C" function pointer — no noundef on return.
int call_extern_c_fp(int x) {
  return extern_c_fp(x);
}

// CIR-LABEL: define {{.*}}i32 @_Z16call_extern_c_fpi
// CIR: call i32 %{{.*}}(i32 noundef %{{.*}})

// OGCG-LABEL: define {{.*}}i32 @_Z16call_extern_c_fpi
// OGCG: call i32 %{{.*}}(i32 noundef %{{.*}})

// Indirect call through C++ function pointer — noundef on return.
int call_cpp_fp(int x) {
  return cpp_fp(x);
}

// CIR-LABEL: define {{.*}}i32 @_Z11call_cpp_fpi
// CIR: call noundef i32 %{{.*}}(i32 noundef %{{.*}})

// OGCG-LABEL: define {{.*}}i32 @_Z11call_cpp_fpi
// OGCG: call noundef i32 %{{.*}}(i32 noundef %{{.*}})
