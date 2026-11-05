// RUN: %clang_cc1 -std=c++20 -triple aarch64-unknown-linux-gnu -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test for TryCallOp return value coercion. When CC lowering
// coerces a try_call's return type (e.g., struct -> integer on AArch64),
// the coercion ops (store, bitcast, load) must be placed in the normal
// destination block, NOT after the TryCallOp terminator. Three code paths
// in the return value coercion lambda called setInsertionPointAfter(newOp)
// which, for TryCallOp, placed ops after the terminator — creating blocks
// without terminators in the final LLVM IR.

// Pattern A: Struct return with record type coercion
struct Small {
  int a, b;
};

struct Guard { ~Guard(); };
Guard::~Guard() {}

Small make_small();

// CHECK-LABEL: define{{.*}} i32 @_Z12test_recordsv
// CHECK: invoke i64 @_Z10make_smallv()
// CHECK-NEXT:     to label %[[NORMAL:[0-9]+]] unwind
// CHECK: [[NORMAL]]:
int test_records() {
  Guard g;
  try {
    Small s = make_small();
    return s.a + s.b;
  } catch (...) {
    return -1;
  }
}

// Pattern B: Homogeneous float aggregate return (HFA)
struct HFA {
  float x, y;
};

HFA make_hfa();

// CHECK-LABEL: define{{.*}} i32 @_Z8test_hfav
// CHECK: invoke %struct.HFA @_Z8make_hfav()
// CHECK-NEXT:     to label %[[NORMAL2:[0-9]+]] unwind
// CHECK: [[NORMAL2]]:
int test_hfa() {
  Guard g;
  try {
    HFA h = make_hfa();
    return (int)(h.x + h.y);
  } catch (...) {
    return -1;
  }
}

// Pattern C: Void return TryCallOp with indirect args
// (tests LifetimeEndOp placement)
struct LargeArg {
  long data[4];
};

void takes_large(LargeArg a);

// CHECK-LABEL: define{{.*}} void @_Z18test_indirect_argsv
// CHECK: invoke void @_Z11takes_large8LargeArg
// CHECK-NEXT:     to label %[[NORMAL3:[0-9]+]] unwind
void test_indirect_args() {
  Guard g;
  try {
    LargeArg a{};
    takes_large(a);
  } catch (...) {
  }
}

// Pattern D: TryCallOp returning large struct (sret/indirect return)
LargeArg make_large();

// CHECK-LABEL: define{{.*}} void @_Z15test_sret_triedv
// CHECK: invoke void @_Z10make_largev
// CHECK-NEXT:     to label %[[NORMAL4:[0-9]+]] unwind
void test_sret_tried() {
  Guard g;
  try {
    LargeArg l = make_large();
    (void)l.data[0];
  } catch (...) {
  }
}
