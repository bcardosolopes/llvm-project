// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// A struct passed by value and coerced to a single pointer (ABIArgInfo::Direct)
// must have its parameter "uncoerce" alloca/store emitted in the entry block,
// before any use of the parameter. The calling-convention lowering previously
// created the uncoerced alloca at the rewriter's current insertion point (the
// end of the entry block, after the cloned function body), so the alloca landed
// after the operations that use the parameter. That is a use-before-def: the
// verifier rejects it, and with verification off it prints a `<null operand!>`
// and crashes later passes (seen on real code in a constructor consuming a
// by-value unique_ptr parameter).

struct S {
  int *p;
};

int use(S s);

int g(S s) { return *s.p + use(s); }

// CHECK-LABEL: define{{.*}} i32 @_Z1g1S(ptr %0)
// The parameter spill alloca and store must come first, dominating the uses.
// CHECK:         %[[SLOT:.*]] = alloca %struct.S
// CHECK:         store ptr %0, ptr %[[SLOT]]
// CHECK:         getelementptr {{.*}} %[[SLOT]]
