// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Pointer-to-member of anonymous union member - previously crashed with
// cast<FieldDecl> assertion failure in ConstantEmitter::tryEmitPrivate. Now fixed.

struct S {
    char c2;
    union {
        char c;
        int i;
    };
};

char S::*foo = &S::c;

// CHECK: @foo = global i64 4
// OGCG: @foo = global i64 4
