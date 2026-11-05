// RUN: %clang_cc1 %s -triple x86_64-unknown-linux -emit-llvm -o - | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 %s -triple x86_64-unknown-linux -emit-llvm -fclangir -o - | FileCheck %s --check-prefix=CIR

struct A {
  A();
};

__attribute__((noreturn)) int f();

void test() {
  f();
  static A r;
}

// OGCG: @_ZZ4testvE1r = internal global %struct.A zeroinitializer, align 1
// OGCG-NOT: @_ZGVZ4testvE1r
// OGCG-LABEL: define dso_local void @_Z4testv()
// OGCG: call noundef i32 @_Z1fv()
// OGCG-NEXT: unreachable

// CIR: @_ZZ4testvE1r = internal global %struct.A zeroinitializer, align 1
// CIR-NOT: @_ZGVZ4testvE1r
// CIR-LABEL: define dso_local void @_Z4testv()
// CIR: call noundef i32 @_Z1fv()
// CIR-NEXT: unreachable
