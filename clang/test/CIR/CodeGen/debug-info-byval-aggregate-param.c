// Verify that a struct passed by value gets a DW_TAG_formal_parameter (a
// DILocalVariable with an "arg" number) under ClangIR, matching classic
// CodeGen. The parameter's storage alloca is initialized indirectly by the ABI
// (coerce + memcpy on x86_64/aarch64, expanded field stores on i386) rather
// than by a single `store %arg, %alloca`, so the debug-argument recovery must
// look through those shapes.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.x86.cir.ll
// RUN: FileCheck --input-file=%t.x86.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.x86.ogcg.ll
// RUN: FileCheck --input-file=%t.x86.ogcg.ll %s --check-prefix=OGCG

// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.aarch64.cir.ll
// RUN: FileCheck --input-file=%t.aarch64.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.aarch64.ogcg.ll
// RUN: FileCheck --input-file=%t.aarch64.ogcg.ll %s --check-prefix=OGCG

// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.i386.cir.ll
// RUN: FileCheck --input-file=%t.i386.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.i386.ogcg.ll
// RUN: FileCheck --input-file=%t.i386.ogcg.ll %s --check-prefix=OGCG

struct S {
  int a, b, c;
};

int f(struct S s) { return s.a; }

// CIR: #dbg_declare(ptr %{{.*}}, ![[VAR:[0-9]+]], !DIExpression()
// CIR: ![[VAR]] = !DILocalVariable(name: "s", arg: 1,

// OGCG: #dbg_declare(ptr %{{.*}}, ![[VAR:[0-9]+]], !DIExpression()
// OGCG: ![[VAR]] = !DILocalVariable(name: "s", arg: 1,
