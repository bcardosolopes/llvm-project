// RUN: %clang_cc1 -triple x86_64-unknown-unknown -std=gnu11 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -std=gnu11 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// CIR crashed with assertion "Padding cannot be larger than the union"
// when compiling unions/structs with __attribute__((__aligned__(N))).
// The getStorageMember() function tried to reconstruct the layout size from
// member alignments alone, but explicit alignment attributes inflate the
// actual layout size beyond what member alignments predict.

union U2 {
  int x;
  char y[16];
} __attribute__((__aligned__(32)));

struct S2 {
  int x;
  long long y;
  char z[8];
} __attribute__((__aligned__(32)));

// Test that aligned unions compile and produce correct layout.
union U2 global_u2_empty = {};
union U2 global_u2_init = { .x = 42 };

// Test that aligned structs compile and produce correct layout.
struct S2 global_s2_empty = {};
struct S2 global_s2_init = { .x = 1 };

// LLVM: @global_u2_empty = global {{.*}} zeroinitializer, align 32
// LLVM: @global_u2_init = global {{.*}} { i32 42, [28 x i8] zeroinitializer }, align 32
// LLVM: @global_s2_empty = global {{.*}} zeroinitializer, align 32
// LLVM: @global_s2_init = global {{.*}} { i32 1, {{.*}} }, align 32

// OGCG: @global_u2_empty = global {{.*}} zeroinitializer, align 32
// OGCG: @global_u2_init = global {{.*}} { i32 42, [28 x i8] zeroinitializer }, align 32
// OGCG: @global_s2_empty = global {{.*}} zeroinitializer, align 32
// OGCG: @global_s2_init = global {{.*}} { i32 1, {{.*}} }, align 32
