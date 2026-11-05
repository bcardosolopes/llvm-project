// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Verify that constexpr pointers into the middle of a string literal
// (e.g. from string_view::substr) are correctly lowered as global_view
// with array indices, rather than crashing in
// computeGlobalViewIndicesFromFlatOffset.

struct StrView {
  const char *data;
  unsigned long size;
};

const char hello[] = "hello world";
StrView after = { hello + 6, 5 };

// CHECK: cir.global {{.*}} @_ZL5hello = #cir.const_array<"hello world\00"
// CHECK: cir.global {{.*}} @after = #cir.const_record<{
// CHECK-SAME: #cir.global_view<@_ZL5hello, [6 : i32]> : !cir.ptr<!s8i>
// CHECK-SAME: #cir.int<5> : !u64i
// CHECK-SAME: }>
