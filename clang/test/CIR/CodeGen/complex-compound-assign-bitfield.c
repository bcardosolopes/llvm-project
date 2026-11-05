// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that complex compound assignment to a bitfield compiles correctly.
// The _Bool bitfield must be promoted to the complex element type (float)
// before the complex operation, and the result stored back through the
// bitfield lvalue.

struct Bits {
    int pad1: 30;
    bool b: 1;
    int pad2: 1;
};

// CIR-LABEL: @test
// CIR: cir.get_bitfield
// CIR: cir.set_bitfield
//
// LLVM-LABEL: @test
// LLVM: lshr i32
// LLVM: and i32
// LLVM: uitofp
// LLVM: shl i32
// LLVM: or i32
// LLVM: store i32
//
// OGCG-LABEL: @test
// OGCG: lshr i32
// OGCG: and i32
// OGCG: uitofp
// OGCG: shl i32
// OGCG: or i32
// OGCG: store i32
void test(struct Bits *x) {
    x->b += __builtin_complex(-1.0f, 0.0f);
}

// Also test with non-bitfield bool LHS → complex float to verify
// scalar-to-complex cast works for integer types in general.

// CIR-LABEL: @test_bool
// CIR: cir.cast bool_to_float
// CIR: cir.complex.create
//
// LLVM-LABEL: @test_bool
// LLVM: uitofp
// LLVM: fadd float
//
// OGCG-LABEL: @test_bool
// OGCG: uitofp
// OGCG: fadd float
void test_bool(_Bool *b) {
    *b += __builtin_complex(1.0f, 0.0f);
}
