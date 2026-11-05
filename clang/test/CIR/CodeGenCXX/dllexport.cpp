// RUN: %clang_cc1 -triple x86_64-scei-ps4 -fdeclspec -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// RUN: %clang_cc1 -triple x86_64-scei-ps4 -fdeclspec -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify CIR propagates dllexport on function and global variable
// definitions, and dllimport on function/variable declarations for PS4 targets.

struct __declspec(dllexport) S {
  S &operator=(const S &) = default;
  void f();
};
void S::f() {}

__declspec(dllexport) int exported_var = 42;

// dllimport on function declaration
__declspec(dllimport) void imported_func();
void use_func_import() { imported_func(); }

// dllimport on global variable declaration
__declspec(dllimport) extern int imported_var;
int use_var_import() { return imported_var; }

// Functions — dllexport on definitions
// CHECK-DAG: define weak_odr dllexport {{.*}}ptr @_ZN1SaSERKS_(
// CHECK-DAG: define {{.*}}dllexport void @_ZN1S1fEv(
// CHECK-DAG: @exported_var = dllexport global i32 42

// Function declaration — dllimport
// CHECK-DAG: declare dllimport void @_Z13imported_funcv()

// Global variable declaration — dllimport
// CHECK-DAG: @imported_var = external dllimport global i32

// OGCG-DAG: define weak_odr dllexport {{.*}}ptr @_ZN1SaSERKS_(
// OGCG-DAG: define {{.*}}dllexport void @_ZN1S1fEv(
// OGCG-DAG: @exported_var = dllexport global i32 42
// OGCG-DAG: declare dllimport void @_Z13imported_funcv()
// OGCG-DAG: @imported_var = external dllimport global i32
