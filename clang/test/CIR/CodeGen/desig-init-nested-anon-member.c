// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.og.ll

// Designated initializer updating a nested anonymous struct member
// through a named struct path (.B.A) previously crashed because
// VisitDesignatedInitUpdateExpr couldn't decompose a ZeroAttr base
// into patchable fields, and Visit() was used instead of
// tryEmitPrivateForMemory() for field values.

struct Base {
  struct {
    int A;
  };
};
struct Derived {
  struct Base B;
};

// Basic case: designated init update with preceding empty init.
struct Derived D1 = {{}, .B.A = 42};

// CIR: cir.global  external  @D1 = #cir.const_record<{#cir.const_record<{#cir.const_record<{#cir.int<42> : !s32i}>
// LLVM: @D1 = global %struct.Derived { %struct.Base { %struct.anon{{.*}} { i32 42 } } }
// OGCG: @D1 = global %struct.Derived { %struct.Base { %struct.anon { i32 42 } } }

// Direct designated init without preceding empty init.
struct Derived D2 = {.B.A = 100};

// CIR: cir.global  external  @D2 = #cir.const_record<{#cir.const_record<{#cir.const_record<{#cir.int<100> : !s32i}>
// LLVM: @D2 = global %struct.Derived { %struct.Base { %struct.anon{{.*}} { i32 100 } } }
// OGCG: @D2 = global %struct.Derived { %struct.Base { %struct.anon { i32 100 } } }
