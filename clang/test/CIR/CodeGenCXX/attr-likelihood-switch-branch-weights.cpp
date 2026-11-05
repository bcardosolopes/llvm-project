// RUN: %clang_cc1 -O1 -disable-llvm-passes -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll -std=c++20
// RUN: FileCheck --input-file=%t.ll %s

extern volatile int i;

// CHECK-LABEL: define dso_local void @_Z8OneCaseUv(
// CHECK: switch i32 {{.*}}, label %[[EPILOG0:.*]] [
// CHECK-NEXT: i32 1, label %[[BBU:.*]]
// CHECK-NEXT: ], !prof [[PROF0:![0-9]+]]
void OneCaseU() {
  switch (i) {
    [[unlikely]] case 1:
      ++i;
      break;
  }
}

// CHECK-LABEL: define dso_local void @_Z20CasesFallthroughNNLNv(
// CHECK: switch i32 {{.*}}, label %[[EPILOG:.*]] [
// CHECK-NEXT: i32 1, label %[[BB0:.*]]
// CHECK-NEXT: i32 2, label %[[BB0]]
// CHECK-NEXT: i32 3, label %[[BB1:.*]]
// CHECK-NEXT: i32 4, label %[[BB1]]
// CHECK-NEXT: ], !prof [[PROF1:![0-9]+]]
void CasesFallthroughNNLN() {
  switch (i) {
  case 1:
  case 2:
    [[likely]] case 3:
  case 4:
    break;
  }
}

// CHECK-LABEL: define dso_local void @_Z20CasesFallthroughNNUNv(
// CHECK: switch i32 {{.*}}, label %[[EPILOG1:.*]] [
// CHECK-NEXT: i32 1, label %[[BB2:.*]]
// CHECK-NEXT: i32 2, label %[[BB2]]
// CHECK-NEXT: i32 3, label %[[BB3:.*]]
// CHECK-NEXT: i32 4, label %[[BB3]]
// CHECK-NEXT: ], !prof [[PROF2:![0-9]+]]
void CasesFallthroughNNUN() {
  switch (i) {
  case 1:
  case 2:
    [[unlikely]] case 3:
  case 4:
    break;
  }
}

// CHECK-LABEL: define dso_local void @_Z28CasesFallthroughRangeSmallLNv(
// CHECK: switch i32 {{.*}}, label %[[EPILOG2:.*]] [
// CHECK-NEXT: i32 102, label %[[BB4:.*]]
// CHECK-NEXT: i32 103, label %[[BB5:.*]]
// CHECK-NEXT: i32 104, label %[[BB5]]
// CHECK-NEXT: i32 1, label %[[BB6:.*]]
// CHECK-NEXT: i32 2, label %[[BB6]]
// CHECK-NEXT: i32 3, label %[[BB6]]
// CHECK-NEXT: i32 4, label %[[BB6]]
// CHECK-NEXT: i32 5, label %[[BB6]]
// CHECK-NEXT: ], !prof [[PROF3:![0-9]+]]
void CasesFallthroughRangeSmallLN() {
  switch (i) {
  case 1 ... 5:
    ++i;
  case 102:
    [[likely]] case 103:
  case 104:
    break;
  }
}

// CHECK-LABEL: define dso_local void @_Z29CasesFallthroughRangeLargeLLNv(
// CHECK: switch i32 [[COND:%.*]], label %[[RANGE:.*]] [
// CHECK-NEXT: i32 1003, label %[[TARGET:.*]]
// CHECK-NEXT: i32 104, label %[[TARGET]]
// CHECK-NEXT: ], !prof [[PROF4:![0-9]+]]
// CHECK: [[RANGE]]:
// CHECK-NEXT: [[DIFF:%.*]] = sub i32 [[COND]], 0
// CHECK-NEXT: [[INBOUNDS:%.*]] = icmp ule i32 [[DIFF]], 64
// CHECK-NEXT: [[EXPECT:%.*]] = call i1 @llvm.expect.i1(i1 [[INBOUNDS]], i1 true)
// CHECK-NEXT: br i1 [[EXPECT]], label %[[RANGEDEST:.*]], label %[[EPILOG3:.*]]
void CasesFallthroughRangeLargeLLN() {
  switch (i) {
    [[likely]] case 0 ... 64:
    [[likely]] case 1003:
  case 104:
    break;
  }
}

// CHECK-LABEL: define dso_local void @_Z15OneCaseDefaultUv(
// CHECK: switch i32 {{.*}}, label %[[DEFAULT:.*]] [
// CHECK-NEXT: i32 1, label %[[EPILOG4:.*]]
// CHECK-NEXT: ], !prof [[PROF5:![0-9]+]]
void OneCaseDefaultU() {
  switch (i) {
  case 1:
    break;
  [[unlikely]] default:
    break;
  }
}

// CHECK: [[PROF0]] = !{!"branch_weights", i32 536870912, i32 1}
// CHECK: [[PROF1]] = !{!"branch_weights", i32 143165577, i32 143165577, i32 143165577, i32 715827883, i32 143165577}
// CHECK: [[PROF2]] = !{!"branch_weights", i32 214748365, i32 214748365, i32 214748365, i32 1, i32 214748365}
// CHECK: [[PROF3]] = !{!"branch_weights", i32 79536432, i32 79536432, i32 715827883, i32 79536432, i32 79536432, i32 79536432, i32 79536432, i32 79536432, i32 79536432}
// CHECK: [[PROF4]] = !{!"branch_weights", i32 238609295, i32 715827883, i32 238609295}
// CHECK: [[PROF5]] = !{!"branch_weights", i32 1, i32 536870912}
