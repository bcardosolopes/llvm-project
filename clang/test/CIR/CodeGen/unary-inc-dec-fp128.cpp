// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -std=c++20 -w %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 -w %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -std=c++20 -w %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

__float128 x;

void test_inc() {
  ++x;
}

// CIR-LABEL: @_Z8test_incv
// CIR: cir.binop(add, %{{.*}}, %{{.*}}) : !cir.f128

// LLVM-LABEL: @_Z8test_incv
// LLVM: fadd fp128 %{{.*}}, 0xL00000000000000003FFF000000000000

// OGCG-LABEL: @_Z8test_incv
// OGCG: fadd fp128 %{{.*}}, 0xL00000000000000003FFF000000000000

void test_dec() {
  --x;
}

// CIR-LABEL: @_Z8test_decv
// CIR: cir.binop(add, %{{.*}}, %{{.*}}) : !cir.f128

// LLVM-LABEL: @_Z8test_decv
// LLVM: fadd fp128 %{{.*}}, 0xL0000000000000000BFFF000000000000

// OGCG-LABEL: @_Z8test_decv
// OGCG: fadd fp128 %{{.*}}, 0xL0000000000000000BFFF000000000000
