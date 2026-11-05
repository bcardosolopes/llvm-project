// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fclangir -emit-llvm -std=c++11 -O1 -disable-llvm-passes -o - %s \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -emit-llvm -std=c++11 -O1 -disable-llvm-passes -o - %s \
// RUN:   | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fclangir -emit-llvm -std=c++11 -O1 -disable-llvm-passes -o - %s \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -emit-llvm -std=c++11 -O1 -disable-llvm-passes -o - %s \
// RUN:   | FileCheck %s --check-prefix=OGCG

struct Base0 {
  void nonvirtual0();
  virtual void virtual1();
};

struct Derived0 : Base0 {
  void virtual1() override;
  void nonvirtual5();
};

typedef void (Base0::*MethodTy0)();
typedef void (Derived0::*MethodTy1)();

struct Class0 {
  MethodTy1 m0;
};

MethodTy1 gmethod0 = reinterpret_cast<MethodTy1>(&Base0::nonvirtual0);
MethodTy0 gmethod1 = reinterpret_cast<MethodTy0>(&Derived0::nonvirtual5);

void initList(MethodTy1);

void testArrayInit() {
  MethodTy1 p0[] = {&Base0::nonvirtual0};
  Class0 c0{&Base0::nonvirtual0};
  initList(p0[0]);
  initList(c0.m0);
}

namespace testNoexceptConversion {
struct S {
  void nonvirtual_noexcept() noexcept;
  virtual void virtual_noexcept() noexcept;
};

void (S::*mfptr1)() = &S::nonvirtual_noexcept;
void (S::*mfptr3_noexcept)() noexcept = &S::nonvirtual_noexcept;
} // namespace testNoexceptConversion

// CIR-DAG: @gmethod0 = global { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base011nonvirtual0Ev, i32 0, i64 35591) to i64), i64 0 }, align 8
// CIR-DAG: @gmethod1 = global { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN8Derived011nonvirtual5Ev, i32 0, i64 22163) to i64), i64 0 }, align 8
// CIR-DAG: @__const._Z13testArrayInitv.p0 = private unnamed_addr constant [1 x { i64, i64 }] [{ i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base011nonvirtual0Ev, i32 0, i64 35591) to i64), i64 0 }], align 8
// CIR-DAG: @__const._Z13testArrayInitv.c0 = private unnamed_addr constant %struct.Class0 { { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base011nonvirtual0Ev, i32 0, i64 35591) to i64), i64 0 } }, align 8
// CIR-DAG: @_ZN22testNoexceptConversion6mfptr1E = global { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN22testNoexceptConversion1S19nonvirtual_noexceptEv, i32 0, i64 54461) to i64), i64 0 }, align 8
// CIR-DAG: @_ZN22testNoexceptConversion15mfptr3_noexceptE = global { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN22testNoexceptConversion1S19nonvirtual_noexceptEv, i32 0, i64 54461) to i64), i64 0 }, align 8

// OGCG-DAG: @gmethod0 = global { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base011nonvirtual0Ev, i32 0, i64 35591) to i64), i64 0 }, align 8
// OGCG-DAG: @gmethod1 = global { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN8Derived011nonvirtual5Ev, i32 0, i64 22163) to i64), i64 0 }, align 8
// OGCG-DAG: @__const._Z13testArrayInitv.p0 = private unnamed_addr constant [1 x { i64, i64 }] [{ i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base011nonvirtual0Ev, i32 0, i64 35591) to i64), i64 0 }], align 8
// OGCG-DAG: @__const._Z13testArrayInitv.c0 = private unnamed_addr constant %struct.Class0 { { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base011nonvirtual0Ev, i32 0, i64 35591) to i64), i64 0 } }, align 8
// OGCG-DAG: @_ZN22testNoexceptConversion6mfptr1E = global { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN22testNoexceptConversion1S19nonvirtual_noexceptEv, i32 0, i64 54461) to i64), i64 0 }, align 8
// OGCG-DAG: @_ZN22testNoexceptConversion15mfptr3_noexceptE = global { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN22testNoexceptConversion1S19nonvirtual_noexceptEv, i32 0, i64 54461) to i64), i64 0 }, align 8
