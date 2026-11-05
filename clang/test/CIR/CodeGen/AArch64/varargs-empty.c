// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s

// REQUIRES: aarch64-registered-target

// Test that empty struct va_arg does not generate register/stack lowering.
// OGCG optimizes empty records to ret void; CIR should skip the AAPCS
// expansion entirely for zero-sized record types.

typedef __builtin_va_list va_list;
extern void *the_list;

typedef struct {} empty;

void empty_record_test(void) {
// CHECK-LABEL: define dso_local void @empty_record_test
// OGCG-LABEL: define dso_local void @empty_record_test
  __builtin_va_arg(*(va_list *)&the_list, empty);

  // CIR should NOT generate any register/stack va_arg lowering for empty types.
  // CHECK-NOT: icmp
  // CHECK-NOT: getelementptr
  // CHECK-NOT: ptrmask
  // CHECK: ret void
  // OGCG-NOT: icmp
  // OGCG-NOT: getelementptr
  // OGCG-NOT: ptrmask
  // OGCG: ret void
}
