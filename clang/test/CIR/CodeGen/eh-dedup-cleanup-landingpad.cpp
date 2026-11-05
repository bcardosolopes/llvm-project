// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM

// CIRGen gives every unwind edge its own landing pad. Sharing the cleanup code
// is not enough: each surviving pad is a separate `landingpad` and gets its own
// LSDA call-site entry, where OGCG points consecutive invokes at one pad and
// merges them into a single call-site range. Pads doing identical work are
// deduplicated so the call-site table can collapse the same way.

struct Guard {
  Guard();
  ~Guard();
};

void may_throw();

// Every unwind edge here destroys the same one object, so one landing pad
// serves all three invokes.
void test_equal_cleanups() {
  Guard g;
  may_throw();
  may_throw();
  may_throw();
}

// LLVM-LABEL: define dso_local void @_Z19test_equal_cleanupsv(
// LLVM: invoke void @_Z9may_throwv()
// LLVM-NEXT: to label %{{.*}} unwind label %[[PAD:.*]]
// LLVM: invoke void @_Z9may_throwv()
// LLVM-NEXT: to label %{{.*}} unwind label %[[PAD]]
// LLVM: invoke void @_Z9may_throwv()
// LLVM-NEXT: to label %{{.*}} unwind label %[[PAD]]
// LLVM-COUNT-1: landingpad { ptr, i32 }
// LLVM-NOT: landingpad { ptr, i32 }
// LLVM: {{^}}}

// The two scopes destroy different sets, so they keep separate pads -- but the
// edges that agree still share one.
void test_growing_cleanups() {
  Guard a;
  may_throw();
  Guard b;
  may_throw();
}

// LLVM-LABEL: define dso_local void @_Z21test_growing_cleanupsv(
// LLVM: invoke void @_Z9may_throwv()
// LLVM-NEXT: to label %{{.*}} unwind label %[[OUTER:.*]]
// LLVM: invoke void @_ZN5GuardC1Ev(
// LLVM-NEXT: to label %{{.*}} unwind label %[[OUTER]]
// LLVM: invoke void @_Z9may_throwv()
// LLVM-NEXT: to label %{{.*}} unwind label %[[INNER:.*]]
// LLVM-NOT: unwind label
// LLVM-COUNT-2: landingpad { ptr, i32 }
// LLVM-NOT: landingpad { ptr, i32 }
// LLVM: {{^}}}
