// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Test that struct coercion returns do not leave a dead load of the
// struct type behind. The return should only have the coerced load
// (e.g., i64) before the ret instruction.

struct S {
    int x, y;
};

S make() {
    S s;
    s.x = 1;
    s.y = 2;
    return s;
}

// Both CIR and OGCG should have a single i64 load for the coerced return,
// with no dead struct load left behind.
// CIR-LABEL: define {{.*}}i64 @_Z4makev()
// CIR-NOT: load %struct.S
// CIR: load i64
// CIR: ret i64

// OGCG-LABEL: define {{.*}}i64 @_Z4makev()
// OGCG-NOT: load %struct.S
// OGCG: ret i64
