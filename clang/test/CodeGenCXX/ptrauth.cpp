// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -emit-llvm -std=c++11 -fexceptions -fcxx-exceptions -o %t %s
// RUN: FileCheck %s --check-prefix=CALL < %t
// RUN: FileCheck %s --check-prefix=TERM < %t
// RUN: %if cir-support %{ %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -emit-llvm -std=c++11 -fexceptions -fcxx-exceptions -fclangir -o %t %s %}
// RUN: FileCheck %s --check-prefix=CALL < %t
// RUN: FileCheck %s --check-prefix=FCLANGIR < %t
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fptrauth-calls -emit-llvm -std=c++11 -fexceptions -fcxx-exceptions -o %t %s
// RUN: FileCheck %s --check-prefix=CALL < %t
// RUN: FileCheck %s --check-prefix=TERM < %t
// RUN: %if cir-support %{ %clang_cc1 -triple aarch64-linux-gnu -fptrauth-calls -emit-llvm -std=c++11 -fexceptions -fcxx-exceptions -fclangir -o %t %s %}
// RUN: FileCheck %s --check-prefix=CALL < %t
// RUN: FileCheck %s --check-prefix=FCLANGIR < %t

void f(void);
auto &f_ref = f;

// CALL: define {{(dso_local )?}}void @_Z1gv(
// CALL: call void ptrauth (ptr @_Z1fv, i32 0)() [ "ptrauth"(i32 0, i64 0) ]
// FCLANGIR: @f_ref = constant ptr ptrauth (ptr @_Z1fv, i32 0), align 8

void g() { f_ref(); }

void foo1();

void test_terminate() noexcept {
  foo1();
}

// TERM: define {{(dso_local )?}}void @_ZSt9terminatev() #[[ATTR4:.*]] {

namespace std {
  void terminate() noexcept {
  }
}

// TERM: attributes #[[ATTR4]] = {{{.*}}"ptrauth-calls"{{.*}}}
