// RUN: %clang_cc1 -triple x86_64-unknown-linux -fexperimental-call-graph-section \
// RUN:   -fclangir -emit-llvm -o - %s | FileCheck %s

class Cls {
public:
  int method(char a, float b, double c) { return 0; }
};

int free_fn(char a, float b, double c) { return 0; }

int use_method(Cls *obj) {
  auto method = &Cls::method;
  return (obj->*method)(0, 0.0f, 0.0);
}

int use_free() {
  int (*fp)(char, float, double) = free_fn;
  return fp(0, 0.0f, 0.0);
}

// CHECK-LABEL: define dso_local noundef i32 @_Z7free_fncfd(
// CHECK-SAME: !type [[FREE_TY:![0-9]+]]
// CHECK-LABEL: define linkonce_odr noundef i32 @_ZN3Cls6methodEcfd(
// CHECK-SAME: !type [[FREE_TY]]
// CHECK-LABEL: define dso_local noundef i32 @_Z10use_methodP3Cls(
// CHECK: call noundef i32 %{{.*}}(ptr noundef nonnull align 1 dereferenceable(1) %{{.*}}, i8 noundef signext 0, float noundef 0.000000e+00, double noundef 0.000000e+00), !callee_type [[METHOD_CT:![0-9]+]]
// CHECK-LABEL: define dso_local noundef i32 @_Z8use_freev(
// CHECK: call noundef i32 %{{.*}}(i8 noundef signext 0, float noundef 0.000000e+00, double noundef 0.000000e+00), !callee_type [[METHOD_CT]]
// CHECK: [[FREE_TY]] = !{i64 0, !"_ZTSFicfdE.generalized"}
// CHECK: [[METHOD_CT]] = !{[[FREE_TY]]}
