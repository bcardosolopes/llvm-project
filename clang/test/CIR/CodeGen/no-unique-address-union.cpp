// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -std=c++17 -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -std=c++17 -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Test that [[no_unique_address]] union member in a class with an empty base
// does not crash during record layout (was calling SmallVector::back() on empty
// vector because union had null base subobject type).

struct Empty {};

class UnionClass : Empty {
    [[no_unique_address]] union {
        int I;
        char C;
    } U;
    int C;
};
UnionClass L;

// CIR: !rec_{{.*}} = !cir.record<union "{{.*}}" {!s32i, !s8i}>
// CIR: !rec_UnionClass = !cir.record<class "UnionClass" {!rec_{{.*}}, !s32i}>
// CIR: cir.global external @L = #cir.zero : !rec_UnionClass

// LLVM: %class.UnionClass = type { %union.{{.*}}, i32 }
// LLVM: @L = global %class.UnionClass zeroinitializer, align 4

// OGCG: %class.UnionClass = type { %union.{{.*}}, i32 }
// OGCG: @L = global %class.UnionClass zeroinitializer, align 4
