// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Union with anonymous struct containing a self-referential data member
// pointer. Previously crashed because the union type was emitted with an
// empty body (no members tracked when non-zero-initializable).

union C {
  struct {
    int C::*memptr;
  };
};

C n{};

// CHECK: !rec_anon{{.*}} = !cir.record<struct "anon.0" {!s64i}>
// CHECK: !rec_C = !cir.record<union "C" {!rec_anon{{.*}}}>
// Null data member pointers lower to -1 (Itanium ABI); the initializer
// must be lowered together with the record members.
// CHECK: cir.global external @n = #cir.const_record<{#cir.const_record<{#cir.int<-1> : !s64i}
