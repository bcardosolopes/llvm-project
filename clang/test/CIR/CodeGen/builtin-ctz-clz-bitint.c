// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -std=c23 %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c23 %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -std=c23 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test that cir.ctz and cir.clz accept _BitInt types with non-standard widths.

int test_ctzg_bitint7(unsigned _BitInt(7) x) {
  return __builtin_ctzg(x);
}

// CIR-LABEL: test_ctzg_bitint7
// CIR:         {{%.+}} = cir.ctz {{%.+}} poison_zero : <u, 7>

// LLVM-LABEL: test_ctzg_bitint7
// LLVM:         {{%.+}} = call i7 @llvm.cttz.i7(i7 {{%.+}}, i1 true)

// OGCG-LABEL: test_ctzg_bitint7
// OGCG:         {{%.+}} = call i7 @llvm.cttz.i7(i7 {{%.+}}, i1 true)

int test_clzg_bitint7(unsigned _BitInt(7) x) {
  return __builtin_clzg(x);
}

// CIR-LABEL: test_clzg_bitint7
// CIR:         {{%.+}} = cir.clz {{%.+}} poison_zero : <u, 7>

// LLVM-LABEL: test_clzg_bitint7
// LLVM:         {{%.+}} = call i7 @llvm.ctlz.i7(i7 {{%.+}}, i1 true)

// OGCG-LABEL: test_clzg_bitint7
// OGCG:         {{%.+}} = call i7 @llvm.ctlz.i7(i7 {{%.+}}, i1 true)

int test_ctzg_bitint128(unsigned _BitInt(128) x) {
  return __builtin_ctzg(x);
}

// CIR-LABEL: test_ctzg_bitint128
// CIR:         {{%.+}} = cir.ctz {{%.+}} poison_zero : !u128i

// LLVM-LABEL: test_ctzg_bitint128
// LLVM:         {{%.+}} = call i128 @llvm.cttz.i128(i128 {{%.+}}, i1 true)

// OGCG-LABEL: test_ctzg_bitint128
// OGCG:         {{%.+}} = call i128 @llvm.cttz.i128(i128 {{%.+}}, i1 true)

int test_clzg_bitint128(unsigned _BitInt(128) x) {
  return __builtin_clzg(x);
}

// CIR-LABEL: test_clzg_bitint128
// CIR:         {{%.+}} = cir.clz {{%.+}} poison_zero : !u128i

// LLVM-LABEL: test_clzg_bitint128
// LLVM:         {{%.+}} = call i128 @llvm.ctlz.i128(i128 {{%.+}}, i1 true)

// OGCG-LABEL: test_clzg_bitint128
// OGCG:         {{%.+}} = call i128 @llvm.ctlz.i128(i128 {{%.+}}, i1 true)
