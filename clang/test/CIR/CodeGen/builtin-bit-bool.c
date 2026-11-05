// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -std=c23 %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c23 %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -std=c23 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// The generic bit-count builtins accept a scalar _Bool. CIRGen keeps bools as
// cir.bool, so the operand must be promoted to a 1-bit unsigned integer before
// the bit op. Keeping the 1-bit width matches classic clang, which lowers to
// the i1 intrinsics (llvm.ctpop.i1 / llvm.ctlz.i1 / llvm.cttz.i1).

int test_popcountg_bool(_Bool b) {
  return __builtin_popcountg(b);
}

// CIR-LABEL: test_popcountg_bool
// CIR:         [[TMP:%.+]] = cir.cast bool_to_int {{%.+}} : !cir.bool -> !cir.int<u, 1>
// CIR:         {{%.+}} = cir.popcount [[TMP]] : !cir.int<u, 1>

// LLVM-LABEL: test_popcountg_bool
// LLVM:         {{%.+}} = call i1 @llvm.ctpop.i1(i1 {{%.+}})

// OGCG-LABEL: test_popcountg_bool
// OGCG:         {{%.+}} = call i1 @llvm.ctpop.i1(i1 {{%.+}})

int test_clzg_bool(_Bool b) {
  return __builtin_clzg(b, 3);
}

// CIR-LABEL: test_clzg_bool
// CIR:         [[TMP:%.+]] = cir.cast bool_to_int {{%.+}} : !cir.bool -> !cir.int<u, 1>
// CIR:         {{%.+}} = cir.clz [[TMP]] poison_zero : <u, 1>

// LLVM-LABEL: test_clzg_bool
// LLVM:         {{%.+}} = call i1 @llvm.ctlz.i1(i1 {{%.+}}, i1 true)

// OGCG-LABEL: test_clzg_bool
// OGCG:         {{%.+}} = call i1 @llvm.ctlz.i1(i1 {{%.+}}, i1 true)

int test_ctzg_bool(_Bool b) {
  return __builtin_ctzg(b, 3);
}

// CIR-LABEL: test_ctzg_bool
// CIR:         [[TMP:%.+]] = cir.cast bool_to_int {{%.+}} : !cir.bool -> !cir.int<u, 1>
// CIR:         {{%.+}} = cir.ctz [[TMP]] poison_zero : <u, 1>

// LLVM-LABEL: test_ctzg_bool
// LLVM:         {{%.+}} = call i1 @llvm.cttz.i1(i1 {{%.+}}, i1 true)

// OGCG-LABEL: test_ctzg_bool
// OGCG:         {{%.+}} = call i1 @llvm.cttz.i1(i1 {{%.+}}, i1 true)
