// RUN: %clang_cc1 -O1 -disable-llvm-passes -triple x86_64-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// A `?:` condition is materialized into a single value, not turned into a nest
// of ifs. The nested form used to hand the then statement AND the else
// statement to both arms of the conditional, emitting two copies of each; OGCG
// instead branches both arms to one shared if.then/if.else. Until CIR can
// express that shared successor, materializing is what keeps the body count
// right. The likelihood attribute survives -- it lands on one llvm.expect.i1
// applied to the merged condition rather than on one per arm.

extern bool a();
extern bool b();
extern bool c();

// CHECK-LABEL: define dso_local void @_Z3tl2Ri(
// CHECK: [[CALL:%.*]] = call noundef zeroext i1 @_Z1av()
// CHECK: br i1 [[CALL]], label %[[TRUE:.*]], label %[[FALSE:.*]]
// CHECK: [[TRUE]]:
// CHECK: [[TRUECALL:%.*]] = call noundef zeroext i1 @_Z1bv()
// CHECK: [[FALSE]]:
// CHECK: [[FALSECALL:%.*]] = call noundef zeroext i1 @_Z1cv()
// CHECK: [[PHI:%.*]] = phi i1 [ [[FALSECALL]], %[[FALSE]] ], [ [[TRUECALL]], %[[TRUE]] ]
// CHECK: [[EXPECT:%.*]] = call i1 @llvm.expect.i1(i1 [[PHI]], i1 true)
// CHECK: br i1 [[EXPECT]], label %[[THEN:.*]], label %[[ELSE:.*]]
// Exactly one copy of each arm's body, and exactly one expect.
// CHECK-NOT: llvm.expect.i1
// CHECK: ret void
void tl2(int &i) {
  if (a() ? b() : c()) [[likely]] {
    ++i;
  } else {
    --i;
  }
}

// CHECK-LABEL: define dso_local void @_Z3tu2Ri(
// CHECK: [[CALL:%.*]] = call noundef zeroext i1 @_Z1av()
// CHECK: br i1 [[CALL]], label %[[TRUE:.*]], label %[[FALSE:.*]]
// CHECK: [[TRUE]]:
// CHECK: [[TRUECALL:%.*]] = call noundef zeroext i1 @_Z1bv()
// CHECK: [[FALSE]]:
// CHECK: [[FALSECALL:%.*]] = call noundef zeroext i1 @_Z1cv()
// CHECK: [[PHI:%.*]] = phi i1 [ [[FALSECALL]], %[[FALSE]] ], [ [[TRUECALL]], %[[TRUE]] ]
// CHECK: [[EXPECT:%.*]] = call i1 @llvm.expect.i1(i1 [[PHI]], i1 false)
// CHECK: br i1 [[EXPECT]], label %[[THEN:.*]], label %[[ELSE:.*]]
// CHECK-NOT: llvm.expect.i1
// CHECK: ret void
void tu2(int &i) {
  if (a() ? b() : c()) [[unlikely]] {
    ++i;
  } else {
    --i;
  }
}
