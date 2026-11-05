// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Test that tryEmitAsConstant properly handles const/constexpr variables,
// matching OGCG behavior by constant-folding uses of compile-time constant
// variables instead of emitting loads from globals.

// --- const int local variable ---
int test_const_local() {
  const int x = 42;
  return x;
}

// CIR-LABEL: @_Z16test_const_localv
// CIR: cir.alloca !s32i, {{.*}} ["x", init, const]
// CIR: %[[C42:.*]] = cir.const #cir.int<42> : !s32i
// CIR: cir.store{{.*}} %[[C42]], %{{.*}}
// CIR: %[[FOLDED:.*]] = cir.const #cir.int<42> : !s32i
// CIR: cir.return %[[FOLDED]] : !s32i

// LLVM-LABEL: @_Z16test_const_localv
// LLVM: store i32 42, ptr %{{.*}}, align 4
// LLVM: ret i32 42

// --- constexpr variable ---
constexpr int SIZE = 100;
int test_constexpr_global() {
  return SIZE;
}

// CIR-LABEL: @_Z21test_constexpr_globalv
// CIR: cir.const #cir.int<100> : !s32i

// LLVM-LABEL: @_Z21test_constexpr_globalv
// LLVM: ret i32 100

// --- const int parameter (should NOT be constant-folded) ---
int test_const_param(const int x) {
  return x;
}

// CIR-LABEL: @_Z16test_const_parami
// CIR: cir.load

// LLVM-LABEL: @_Z16test_const_parami
// LLVM: ret i32 %{{.*}}

// --- enum constant (already supported, ensure it still works) ---
enum Color { RED = 1, GREEN = 2, BLUE = 3 };
int test_enum() {
  return GREEN;
}

// CIR-LABEL: @_Z9test_enumv
// CIR: cir.const #cir.int<2> : !s32i

// LLVM-LABEL: @_Z9test_enumv
// LLVM: ret i32 2

// --- static constexpr member ---
struct Config {
  static constexpr int MAX = 256;
};
int test_static_constexpr() {
  return Config::MAX;
}

// CIR-LABEL: @_Z21test_static_constexprv
// CIR: cir.const #cir.int<256> : !s32i

// LLVM-LABEL: @_Z21test_static_constexprv
// LLVM: ret i32 256
