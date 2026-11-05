// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test constant lvalue emission for pointers into structs with zero-length
// trailing arrays (a GNU extension commonly used as flexible array members).

struct S {
    int len;
    char data[0];
};

S s = {42};
// CHECK: cir.global {{.*}}@s = #cir.const_record<{#cir.int<42> : !s32i, #cir.zero : !cir.array<!s8i x 0>}>

// Pointer to the zero-length trailing array member.
char *p = s.data;
// CHECK: cir.global {{.*}}@p = #cir.global_view<@s, [1 : i32]> : !cir.ptr<!s8i>
