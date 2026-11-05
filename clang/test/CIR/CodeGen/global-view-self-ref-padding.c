// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// Regression test: a *self-referential* global initializer that takes the
// address of one of its own members must lower to a raw byte offset, not to
// structured (member-index) GEP indices.
//
// While a global's constant initializer is being built, the constant record
// builder may insert an *explicit* padding member into the global's record
// type. Here the `int count` field before the over-aligned pointer arrays
// forces 4 bytes of padding, materialized as a `[4 x i8]` member. That padding
// is only added once the whole initializer is known, which is *after* the
// self-referential `&g.head` constant is emitted. The member indices computed
// for `&g.head` are relative to the not-yet-padded type, so the inserted
// padding member shifts every following member's index by one. Previously the
// self-reference was therefore emitted with stale indices pointing at `slots_b`
// (the array preceding `head`) instead of `head`, lowering to a wrong byte
// offset (one array too small) and silently corrupting the constant.

struct node {
  struct node *prev;
  struct node *next;
};

struct registry {
  int count;        // offset 0
  // 4 bytes padding here (an explicit `[4 x i8]` member in the record type)
  void *slots_a[3]; // offset 8
  void *slots_b[3]; // offset 32
  struct node head; // offset 56
};

// `head.prev` and `head.next` both point at `head` itself (byte offset 56).
struct registry g = {
    .count = 0,
    .head = {&g.head, &g.head},
};

// The self-referential member addresses must use a raw byte offset (56), not
// structured member indices.
// CIR-LABEL: cir.global external @g
// CIR-SAME:  #cir.global_view<@g, byte_offset = 56 : i64>
// CIR-SAME:  #cir.global_view<@g, byte_offset = 56 : i64>

// LLVM: @g = global {{.*}}getelementptr {{.*}}(i8, ptr @g, i64 56){{.*}}getelementptr {{.*}}(i8, ptr @g, i64 56)

// A *non*-self-referential address of the same member, taken from a different
// global, is emitted after `g`'s type (including the padding member) is final.
// It must keep the regular structured member index (member index 4 within the
// padded record: count, [4 x i8], slots_a, slots_b, head). This guards against
// over-broadly disabling structured indices for record members.
void *ext = &g.head;
// CIR-LABEL: cir.global external @ext
// CIR-SAME:  #cir.global_view<@g, [4 : i32]>
// LLVM: @ext = global ptr getelementptr {{.*}}(i8, ptr @g, i64 56)
