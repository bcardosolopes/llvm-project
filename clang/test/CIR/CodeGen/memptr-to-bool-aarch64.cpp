// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm \
// RUN:   -std=c++20 -w -o - %s | FileCheck %s

// Regression test: member function pointer to bool conversion on AArch64.
// The member pointer is ABI-coerced from {ptr, ptrdiff_t} to array<u64, 2>.
// extract_member must handle array operands for the null check.
// Also verify ARM ABI virtual-bit handling for bool/null comparisons.

struct S {
  int value;
  bool is_valid() const { return value != 0; }
};

typedef bool (S::*MemFnPtr)() const;

bool test_memptr_to_bool(MemFnPtr p) {
  // This triggers member_ptr_to_bool cast which uses extract_member
  // to get the function pointer field for null comparison.
  return p != nullptr;
}

bool test_memptr_eq_null(MemFnPtr p) { return p == nullptr; }
bool test_memptr_ne_null(MemFnPtr p) { return p != nullptr; }

// CHECK-LABEL: @_Z19test_memptr_to_bool
// CHECK: extractvalue { i64, i64 } %{{.*}}, 0
// CHECK: icmp ne i64 %{{.*}}, 0
// CHECK: %[[ADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// CHECK: %[[ORADJ:.*]] = or i64 %[[ADJ]], 0
// CHECK: %[[VBIT:.*]] = and i64 %[[ORADJ]], 1
// CHECK: icmp ne i64 %[[VBIT]], 0
// CHECK: ret i1 %{{.*}}

// CHECK-LABEL: @_Z19test_memptr_eq_null
// CHECK: extractvalue { i64, i64 } %{{.*}}, 0
// CHECK: icmp eq i64 %{{.*}}, 0
// CHECK: %[[LADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// CHECK: %[[ORADJ:.*]] = or i64 %[[LADJ]], 0
// CHECK: %[[ORADJBIT:.*]] = and i64 %[[ORADJ]], 1
// CHECK: %[[ZEROBIT:.*]] = icmp eq i64 %[[ORADJBIT]], 0

// CHECK-LABEL: @_Z19test_memptr_ne_null
// CHECK: extractvalue { i64, i64 } %{{.*}}, 0
// CHECK: icmp ne i64 %{{.*}}, 0
// CHECK: %[[NADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// CHECK: %[[NORADJ:.*]] = or i64 %[[NADJ]], 0
// CHECK: %[[NORADJBIT:.*]] = and i64 %[[NORADJ]], 1
// CHECK: %[[NONZEROBIT:.*]] = icmp ne i64 %[[NORADJBIT]], 0
// CHECK: ret
