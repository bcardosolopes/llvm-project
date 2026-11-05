// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fexceptions -fobjc-exceptions -fobjc-runtime-has-weak -fclangir -o - %s | FileCheck %s --check-prefix=CHECK-MRR

// A local variable declared inside an @finally block must not trip the
// "Decl already exists in LocalDeclMap!" assertion when the protected @try has
// an early exit in addition to normal fall-through. CIRGen re-emits the
// @finally body on every edge leaving the @try, so each textual copy of the
// body must get fresh storage for its locals.

void cleanup(void) __attribute__((nothrow));

// The @finally body (tmp = 0) is emitted on the early-return edge and on the
// normal fall-through edge, each with its own alloca. cleanup() is nothrow, so
// the exceptional handler is elided and only the two normal edges remain.
int finallyLocalReturn(int x) {
  @try {
    if (x)
      return 1;
    cleanup();
  } @finally {
    int tmp = 0;
    (void)tmp;
  }
  return 0;
}

// cleanup() is nothrow, so the exceptional handler is elided and only the two
// normal edges remain. The @finally body (tmp = 0) is emitted on each edge into
// its own alloca: %[[TMP1]] on the early-return edge and a distinct %[[TMP2]]
// on the fall-through edge (%[[TMP2]] is captured but deliberately not reused,
// documenting that it is a separate slot from %[[TMP1]]).
// CHECK-MRR-LABEL: define{{.*}} i32 @finallyLocalReturn(i32 noundef %0)
// The early-return edge stores the return value (1) then runs the @finally body.
// CHECK-MRR: store i32 1,
// CHECK-MRR-NEXT: store i32 0, ptr %[[TMP1:[0-9]+]], align 4
// CHECK-MRR-NEXT: br
// The fall-through edge runs cleanup() then the @finally body into another slot.
// CHECK-MRR: call void @cleanup()
// CHECK-MRR-NEXT: store i32 0, ptr %[[TMP2:[0-9]+]], align 4
// CHECK-MRR: ret i32

// Multiple locals in the @finally body, exercised across the same two edges.
int finallyMultipleLocals(int x) {
  @try {
    if (x)
      return 1;
    cleanup();
  } @finally {
    int a = 0;
    int b = a + 1;
    (void)b;
  }
  return 0;
}

// CHECK-MRR-LABEL: define{{.*}} i32 @finallyMultipleLocals(i32 noundef %0)
// CHECK-MRR: ret i32

// Nested @try/@finally, each with its own local. Both bodies get re-emitted on
// the outer function's exit edges without a LocalDeclMap collision.
void helper(void);
int finallyNestedLocals(int x) {
  @try {
    if (x)
      return 1;
    @try {
      helper();
    } @finally {
      int inner = 7;
      (void)inner;
    }
  } @finally {
    int outer = 3;
    (void)outer;
  }
  return 0;
}

// CHECK-MRR-LABEL: define{{.*}} i32 @finallyNestedLocals(i32 noundef %0)
// CHECK-MRR: ret i32
