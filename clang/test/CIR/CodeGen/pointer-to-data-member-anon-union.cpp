// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Pointer-to-member of anonymous union member. The member is an
// IndirectFieldDecl that must be resolved through chain().front() to
// get the FieldDecl in the parent record.

struct S {
    char c2;
    union {
        char c;
        int i;
    };
};

// Global pointer-to-member initialization through anonymous union.
char S::*globalPtr = &S::c;
// CIR: cir.global external @globalPtr = #cir.int<4> : !s64i
// LLVM: @globalPtr = global i64 4
// OGCG: @globalPtr = global i64 4

// Function returning pointer-to-member of anonymous union member.
char S::*getPtr() { return &S::c; }
// CIR: cir.func {{.*}} @_Z6getPtrv() -> !s64i
// CIR:   %[[VAL:.*]] = cir.const #cir.int<4> : !s64i
// LLVM: define {{.*}} i64 @_Z6getPtrv()
// LLVM:   ret i64 4
// OGCG: define {{.*}} i64 @_Z6getPtrv()
// OGCG:   ret i64 4
