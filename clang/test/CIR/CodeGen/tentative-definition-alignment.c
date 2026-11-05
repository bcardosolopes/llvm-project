// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Tentative definition of a forward-declared struct: the alignment should
// be derived from the completed type, not from the incomplete type at
// the point of declaration.

struct s0;
struct s0 y;
struct s0 *f0(void) {
  return &y;
}
struct s0 {
  int x;
};

// CIR: cir.global external @y = #cir.zero : !rec_s0 {alignment = 4 : i64}
// LLVM: @y = global %struct.s0 zeroinitializer, align 4
// OGCG: @y = {{(dso_local )?}}global %struct.s0 zeroinitializer, align 4

// Also verify that a variable declared after the struct definition works.
struct s0 x;

// CIR: cir.global external @x = #cir.zero : !rec_s0 {alignment = 4 : i64}
// LLVM: @x = global %struct.s0 zeroinitializer, align 4
// OGCG: @x = {{(dso_local )?}}global %struct.s0 zeroinitializer, align 4
