// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

struct Other {
    int x;
};

struct Trivial {
    int x;
    double y;
    decltype(&Other::x) ptr;
};

// This case has a trivial default constructor, but can't be zero-initialized
// because it contains a data member pointer which has a non-zero null value.
Trivial t;

// CHECK: cir.global external @t = #cir.const_record<{#cir.int<0> : !s32i, #cir.fp<0.000000e+00> : !cir.double, #cir.data_member<null> : !cir.data_member<!s32i in !rec_Other>}> : !rec_Trivial
