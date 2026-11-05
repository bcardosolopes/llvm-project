// Verify the block copy helper destroys already-copied non-trivial C++ captures
// when a later capture's copy-constructor throws during _Block_copy (#325-2).
// Before the fix the copy helper emitted plain calls with no unwind edge, so an
// exception from a later capture's copy-ctor leaked the already-copied captures.

// RUN: %clang_cc1 -triple arm64-apple-macosx11 -fblocks -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-llvm -o - %s | FileCheck %s

struct A { A(); A(const A &); ~A(); };
struct B { B(); B(const B &); ~B(); }; // copy-ctor may throw
void take(void (^)(void));

void f() {
  A a;
  B b;
  void (^blk)(void) = ^{ (void)a; (void)b; };
  take(blk);
}

// The copy helper copies A with a plain call, then B with an invoke whose unwind
// edge lands on a cleanup that destroys the already-copied A before resuming.

// CHECK-LABEL: define {{.*}}@__copy_helper_block
// CHECK-SAME:    personality
// CHECK:         call {{.*}}@_ZN1AC1ERKS_
// CHECK:         invoke {{.*}}@_ZN1BC1ERKS_
// CHECK-NEXT:      to label %{{.*}} unwind label %[[LPAD:.*]]
// CHECK:       {{^}}[[LPAD]]:
// CHECK:         landingpad
// CHECK-NEXT:      cleanup
// CHECK:         call {{.*}}@_ZN1AD1Ev
// CHECK:         resume

// A __block (byref) capture copied before a throwing C++ capture must likewise
// be released via _Block_object_dispose on the unwind path.

void g() {
  __block int x = 0;
  B b;
  void (^blk)(void) = ^{ x++; (void)b; };
  take(blk);
}

// CHECK-LABEL: define {{.*}}@__copy_helper_block{{.*}}r40
// CHECK-SAME:    personality
// CHECK:         call void @_Block_object_assign
// CHECK:         invoke {{.*}}@_ZN1BC1ERKS_
// CHECK-NEXT:      to label %{{.*}} unwind label %[[LPAD2:.*]]
// CHECK:       {{^}}[[LPAD2]]:
// CHECK:         landingpad
// CHECK-NEXT:      cleanup
// CHECK:         call void @_Block_object_dispose
// CHECK:         resume
