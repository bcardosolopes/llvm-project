// RUN: %clang_cc1 -triple x86_64-linux-gnu -O1 -disable-llvm-passes -fclangir \
// RUN:   -fexceptions -fcxx-exceptions -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -O1 -disable-llvm-passes \
// RUN:   -fexceptions -fcxx-exceptions -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Verify that glvalue ternary operators with a throw branch preserve TBAA
// metadata on stores through the resulting LValue. Prior to this fix,
// the single-branch path in emitConditionalOperatorLValue dropped TBAA info.

struct S {
  int x;
  float y;
};

void test_ternary_throw_store(bool flag, S &s) {
  (flag ? s : throw 0).x = 42;
}
// CHECK-LABEL: define {{.*}} @_Z24test_ternary_throw_storebR1S(
// CHECK: store i32 42, ptr %{{.*}}, align 4, !tbaa [[S_X_TAG:!.*]]
// CHECK-DAG: [[S_X_TAG]] = !{[[S_TYPE:!.*]], [[INT_TYPE:!.*]], i64 0}
// CHECK-DAG: [[S_TYPE]] = !{!"_ZTS1S", [[INT_TYPE]], i64 0, !{{.*}}, i64 4}
// CHECK-DAG: [[INT_TYPE]] = !{!"int", !{{.*}}, i64 0}

// OGCG-LABEL: define {{.*}} @_Z24test_ternary_throw_storebR1S(
// OGCG: store i32 42, ptr %{{.*}}, align 4, !tbaa [[S_X_TAG:!.*]]
// OGCG-DAG: [[S_X_TAG]] = !{[[S_TYPE:!.*]], [[INT_TYPE:!.*]], i64 0}
// OGCG-DAG: [[S_TYPE]] = !{!"_ZTS1S", [[INT_TYPE]], i64 0, !{{.*}}, i64 4}
// OGCG-DAG: [[INT_TYPE]] = !{!"int", !{{.*}}, i64 0}
