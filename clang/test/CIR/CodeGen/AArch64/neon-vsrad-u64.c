// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -fclangir -disable-O0-optnone -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -fclangir -disable-O0-optnone -fno-clangir-call-conv-lowering \
// RUN:    -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -target-feature +neon \
// RUN:    -disable-O0-optnone -emit-llvm -o %t-og.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-og.ll %s

// Regression test: vsrad_n_u64 must use logical right shift (lshr), not
// left shift (shl). The builtin means "shift right and accumulate":
//   result = acc + (val >> n)
// Previously CIR emitted a left shift, producing wrong results.

typedef unsigned long long uint64_t;

// CIR-LABEL: test_vsrad_n_u64
// CIR: [[SHR:%.*]] = cir.shift(right, {{%.*}} : !u64i, {{%.*}} : !u64i) -> !u64i
// CIR: {{.*}} = cir.binop(add, {{.*}}, [[SHR]]) : !u64i

// LLVM-LABEL: test_vsrad_n_u64(
// LLVM: [[SHR:%.*]] = lshr i64 {{%.*}}, 3
// LLVM: {{%.*}} = add i64 {{%.*}}, [[SHR]]

// OGCG-LABEL: test_vsrad_n_u64(
// OGCG: [[SHR:%.*]] = lshr i64 {{%.*}}, 3
// OGCG: {{%.*}} = add i64 {{%.*}}, [[SHR]]
uint64_t test_vsrad_n_u64(uint64_t acc, uint64_t val) {
  return __builtin_neon_vsrad_n_u64(acc, val, 3);
}

// Shift by 64 on unsigned: result is 0, so acc + 0 = acc.
// No shift instruction should be emitted.

// CIR-LABEL: test_vsrad_n_u64_max
// CIR-NOT: cir.shift
// CIR: cir.return {{.*}} : !u64i

// LLVM-LABEL: test_vsrad_n_u64_max(
// LLVM-NOT: lshr
// LLVM-NOT: shl
// LLVM: ret i64

// OGCG-LABEL: test_vsrad_n_u64_max(
// OGCG-NOT: lshr
// OGCG-NOT: shl
// OGCG: ret i64
uint64_t test_vsrad_n_u64_max(uint64_t acc, uint64_t val) {
  return __builtin_neon_vsrad_n_u64(acc, val, 64);
}
