// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// A constant pointer that points *before* a record-typed global (a negative
// flat offset) cannot be decomposed into record member indices: unlike arrays,
// records have no negative element index. Previously
// computeGlobalViewIndicesFromFlatOffset asserted `offset >= 0` and crashed.
// It must instead fall back to a raw byte offset in GlobalViewAttr, matching
// classic CodeGen which emits a byte-offset GEP for such pointers.

struct S {
  int a;  // offset 0
  int b;  // offset 4
};

S global_s = {1, 2};

// Pointer 8 bytes before the start of the record.
const char *before_ptr = reinterpret_cast<const char *>(&global_s) - 8;

// CIR: cir.global {{.*}} @before_ptr = #cir.global_view<@global_s, byte_offset = -8 : i64>
// LLVM: @before_ptr = global ptr getelementptr (i8, ptr @global_s, i64 -8)

// Pointer derived from a member's address, landing before the record.
const int *neg_member = &global_s.b - 3;

// CIR: cir.global {{.*}} @neg_member = #cir.global_view<@global_s, byte_offset = -8 : i64>
// LLVM: @neg_member = global ptr getelementptr (i8, ptr @global_s, i64 -8)
