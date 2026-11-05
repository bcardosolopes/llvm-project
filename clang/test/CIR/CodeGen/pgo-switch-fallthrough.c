// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fprofile-instrument=clang -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fprofile-instrument=clang -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// A switch case that is fallen into from the previous case must increment its
// profile counter only on the direct switch-dispatch edge, not on the textual
// fall-through edge (classic clang's CodeGenFunction::EmitBlockWithFallThrough
// "skipcount" block). CIR's switch lowering previously emitted the counter at
// the top of the shared case body, which is also the fall-through target, so a
// fallen-into body was counted once per dispatch plus once per fall-through.
// The counter increment is now split into its own dispatch-only block, leaving
// the fall-through edge to reach the body past it.

int f(int x) {
  int r = 0;
  switch (x) {
  case 1:
    r += 10;
  case 2:
    r += 20;
    break;
  }
  return r;
}

// The switch dispatches case 2 to a block holding only its counter increment
// (index 3), which then branches to the shared case body; the case body (the
// fall-through target) is a separate block, so fall-through does not re-count.
// CHECK-LABEL: define{{.*}}@f
// CHECK: i32 2, label %[[C2DISP:[0-9]+]]
// CHECK: [[C2DISP]]:
// CHECK-NEXT: load i64, ptr getelementptr {{.*}}@__profc_f, i32 0, i32 3
// CHECK: store i64 {{.*}}@__profc_f, i32 0, i32 3
// CHECK: br label %[[C2BODY:[0-9]+]]
// CHECK: [[C2BODY]]:
// CHECK: add nsw i32 {{.*}}, 20

// OGCG-LABEL: define{{.*}}@f
// OGCG: i32 2, label %[[O2DISP:[a-z0-9.]+]]
// OGCG: [[O2DISP]]:
// OGCG-NEXT: load i64, ptr getelementptr {{.*}}@__profc_f, i32 0, i32 3
// OGCG: br label %[[O2BODY:[a-z0-9.]+]]
// OGCG: [[O2BODY]]:
// OGCG: add nsw i32 {{.*}}, 20
