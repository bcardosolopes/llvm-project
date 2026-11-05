// RUN: %clang_cc1 -fclangir -triple wasm32-unknown-unknown -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple wasm32-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=OGCG

// RUN: %clang_cc1 -fclangir -triple wasm64-unknown-unknown -emit-llvm %s -o %t.64.ll
// RUN: FileCheck %s --input-file=%t.64.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple wasm64-unknown-unknown -emit-llvm %s -o %t.64.ogcg.ll
// RUN: FileCheck %s --input-file=%t.64.ogcg.ll --check-prefix=OGCG

// Test that the WebAssembly target ABI is correctly implemented under CIR and
// matches classic CodeGen (OGCG):
// - Single-element structs are coerced to the element type (direct).
// - Multi-field aggregates are returned via sret and passed via byval.
// - Empty structs are dropped from both arguments and returns.

struct S1 { int a; };
struct S2 { int a; int b; };
struct E {};

// Single-element struct: coerced to i32 for both return and argument.
// LLVM: define {{.*}}i32 @mk1(i32 {{.*}})
// OGCG: define {{.*}}i32 @mk1(i32 {{.*}})
struct S1 mk1(int x) { struct S1 s = {x}; return s; }

// LLVM: define {{.*}}i32 @u1(i32 {{.*}})
// OGCG: define {{.*}}i32 @u1(i32 {{.*}})
int u1(struct S1 s) { return s.a; }

// Multi-field struct: sret return + byval argument.
// LLVM: define {{.*}}void @mk2(ptr {{.*}}sret(%struct.S2) align 4 {{.*}}, i32 {{.*}})
// OGCG: define {{.*}}void @mk2(ptr {{.*}}sret(%struct.S2) align 4 {{.*}}, i32 {{.*}})
struct S2 mk2(int x) { struct S2 s = {x, x + 1}; return s; }

// LLVM: define {{.*}}i32 @u2(ptr {{.*}}byval(%struct.S2) align 4 {{.*}})
// OGCG: define {{.*}}i32 @u2(ptr {{.*}}byval(%struct.S2) align 4 {{.*}})
int u2(struct S2 s) { return s.a + s.b; }

// Empty struct: dropped from arguments; void return.
// LLVM: define {{.*}}i32 @ue()
// OGCG: define {{.*}}i32 @ue()
int ue(struct E e) { return 7; }

// LLVM: define {{.*}}void @me()
// OGCG: define {{.*}}void @me()
struct E me(void) { struct E e = {}; return e; }
