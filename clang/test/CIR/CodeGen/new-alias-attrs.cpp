// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that nonnull/noundef on operator new return value and noundef on
// the argument are preserved when operator new is declared as an alias.

using size_t = decltype(sizeof(0));
extern "C" char *something(long long x) { return nullptr; }
void *operator new(size_t) __attribute__((alias("something")));

// CHECK: @_Znwm = alias ptr (i64), ptr @something
// OGCG: @_Znwm = {{.*}}alias ptr (i64), ptr @something

// CHECK-LABEL: @__cxx_global_var_init
// CHECK: call noalias noundef nonnull ptr @_Znwm(i64 noundef 4)

// OGCG-LABEL: @__cxx_global_var_init
// OGCG: call noalias noundef nonnull ptr @_Znwm(i64 noundef 4)

int *p = new int;
