// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// Verify that constant lvalue offsets that cannot be decomposed into
// structured record member indices are handled via raw byte offsets
// in GlobalViewAttr, instead of crashing in
// computeGlobalViewIndicesFromFlatOffset.

// This exercises the tail-padding case: the offset 5 falls past the
// last member (char b at offset 4, size 1) in the 3 bytes of tail padding.

struct S {
  int a;   // offset 0, size 4
  char b;  // offset 4, size 1
  // 3 bytes tail padding (total struct size = 8)
};

S global_s = {1, 2};

// Taking address at byte offset 5 (in the tail padding after 'b').
// This cannot be decomposed into struct member indices.
const char *tail_ptr = reinterpret_cast<const char*>(&global_s) + 5;

// CIR: cir.global {{.*}} @tail_ptr = #cir.global_view<@global_s, byte_offset = 5 : i64>
// LLVM: @tail_ptr = global ptr getelementptr {{.*}}(i8, ptr @global_s, i64 5)
