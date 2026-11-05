// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

struct S {
  int id;
  S(int i);
  ~S();
};

// Case 1: Static local const reference with temporary initializer.
// The temporary's destructor must be registered with __cxa_atexit
// inside the guard block, after the ctor and before guard release.
const S& get_ref() {
  static const S& r = S(42);
  return r;
}

// Case 2: Global rvalue reference with temporary initializer.
// The destructor must be registered during global init.
S &&global_rref = S(50);

// Both CIR and OGCG should register the static local reference
// temporary's destructor inside the guard block.

// LLVM-DAG: call void @_ZN1SC1Ei(ptr {{[^@]*}}@_ZGRZ7get_refvE1r_, i32 noundef 42)
// LLVM-DAG: call i32 @__cxa_atexit(ptr {{[^@]*}}@_ZN1SD1Ev, ptr {{[^@]*}}@_ZGRZ7get_refvE1r_

// OGCG-DAG: @_ZN1SC{{[12]}}Ei(ptr {{.*}}@_ZGRZ7get_refvE1r_, i32 {{.*}}42)
// OGCG-DAG: @__cxa_atexit({{.*}}@_ZN1SD{{[12]}}Ev, ptr @_ZGRZ7get_refvE1r_

// Both should register the global rvalue reference temporary's destructor.
// LLVM-DAG: call void @_ZN1SC1Ei(ptr {{[^@]*}}@_ZGR11global_rref_, i32 noundef 50)
// LLVM-DAG: call i32 @__cxa_atexit(ptr {{[^@]*}}@_ZN1SD1Ev, ptr {{[^@]*}}@_ZGR11global_rref_

// OGCG-DAG: @_ZN1SC{{[12]}}Ei(ptr {{.*}}@_ZGR11global_rref_, i32 {{.*}}50)
// OGCG-DAG: @__cxa_atexit({{.*}}@_ZN1SD{{[12]}}Ev, ptr @_ZGR11global_rref_
