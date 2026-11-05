// RUN: %clang_cc1 -fclangir -triple bpf -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple bpf -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=LLVM

// RUN: %clang_cc1 -fclangir -triple bpf -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR

// Test that BPF target ABI lowering is correctly implemented under CIR.
// - Small aggregates (<=64 bits) are coerced to integer type
// - Medium aggregates (65-128 bits) are coerced to [2 x i64]
// - Large aggregates (>128 bits) are passed indirectly via byval pointer
// - Char is sign-extended
// - Aggregate return uses sret

struct Small { int x; };           // 32 bits
struct Medium { long a; long b; }; // 128 bits
struct Big { long a; long b; long c; };  // 192 bits

// LLVM-DAG: define {{.*}}void @use_small(i32 {{.*}})
void use_small(struct Small s) {}

// LLVM-DAG: define {{.*}}void @use_medium([2 x i64] {{.*}})
void use_medium(struct Medium m) {}

// LLVM-DAG: define {{.*}}void @use_big(ptr {{.*}}byval(%struct.Big) align 8 {{.*}})
void use_big(struct Big b) {}

// LLVM-DAG: define {{.*}}void @use_char(i8 {{.*}}signext {{.*}})
void use_char(char c) {}

// LLVM-DAG: define {{.*}}void @ret_small(ptr {{.*}}sret(%struct.Small) align 4 {{.*}})
struct Small ret_small(void) { struct Small s = {0}; return s; }

// CIR: cir.func {{.*}}@use_small
// CIR: cir.func {{.*}}@use_medium
// CIR: cir.func {{.*}}@use_big
// CIR: cir.func {{.*}}@use_char
// CIR: cir.func {{.*}}@ret_small
