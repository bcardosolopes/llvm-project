// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -std=c++20 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// extern "C" functions in C++ mode follow C return semantics — the return
// value should NOT get noundef. Matches OGCG's HasStrictReturn()
// (CGCall.cpp:1906-1936).

// Declaration-only: the common case (e.g., C library functions).
extern "C" int extern_c_decl(int);
int test_decl() { return extern_c_decl(42); }
// CHECK: declare i32 @extern_c_decl(i32 noundef)
// OGCG: declare i32 @extern_c_decl(i32 noundef)

// Definition: extern "C" function defined in this TU.
extern "C" int extern_c_def(int x) { return x + 1; }
// CHECK: define dso_local i32 @extern_c_def(i32 noundef %{{.*}})
// OGCG: define dso_local i32 @extern_c_def(i32 noundef %{{.*}})

// Regular C++ function — should still get noundef on return.
int cpp_func(int x) { return x; }
// CHECK: define dso_local noundef i32 @_Z8cpp_funci(i32 noundef %{{.*}})
// OGCG: define dso_local noundef i32 @_Z8cpp_funci(i32 noundef %{{.*}})

// Regular C++ declaration — should get noundef on return.
int cpp_decl(int);
int test_cpp_decl() { return cpp_decl(42); }
// CHECK: declare noundef i32 @_Z8cpp_decli(i32 noundef)
// OGCG: declare noundef i32 @_Z8cpp_decli(i32 noundef)
