// RUN: %clang_cc1 -triple x86_64-unknown-windows -std=c++11 -fclangir \
// RUN:   -clangir-disable-passes -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-windows -std=c++11 -regcall4 -fclangir \
// RUN:   -clangir-disable-passes -emit-cir %s -o - | FileCheck %s

struct S {
  ~S() {}
};

void f() {
  S s;
}

// CHECK: cir.func no_inline optnone comdat linkonce_odr dso_local @"??1S@@QEAA@XZ"
// CHECK-LABEL: cir.func no_inline optnone dso_local @"?f@@YAXXZ"()
// CHECK: %[[S:.+]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["s"]
// CHECK: cir.call @"??1S@@QEAA@XZ"(%[[S]]) nothrow : (!cir.ptr<!rec_S>) -> ()
