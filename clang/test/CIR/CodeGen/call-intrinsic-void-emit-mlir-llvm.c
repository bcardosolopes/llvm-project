// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-mlir=llvm %s -o - | FileCheck %s --check-prefix=MLIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// A void-returning intrinsic must lower to a ZERO-RESULT `llvm.call_intrinsic`
// (`() -> ()`) in the LLVM dialect, not a single `!llvm.void` result: a
// `!llvm.void` result prints as `-> !llvm.void`, which the parser rejects (so
// the `-emit-mlir=llvm` module fails to round-trip) and which crashes the CIR
// intrinsic-call lowering's `replaceOp` with a result-count mismatch.
//
// Beyond the dialect form, sanity-check the whole pipeline: the
// C -> ClangIR -> LLVM-dialect -> LLVM IR path must produce the same
// `call void @llvm.debugtrap()` as classic (non-ClangIR) CodeGen (OGCG).

void call_void_intrinsic(void) { __builtin_debugtrap(); }

// The LLVM-dialect intermediate carries the void intrinsic as a zero-result op:
// MLIR-LABEL: llvm.func @call_void_intrinsic()
// MLIR:         llvm.call_intrinsic "llvm.debugtrap"() : () -> ()

// ... and it lowers to the identical LLVM IR as classic CodeGen:
// CIR-LABEL:  define dso_local void @call_void_intrinsic()
// CIR:          call void @llvm.debugtrap()
// CIR-NEXT:     ret void

// OGCG-LABEL: define dso_local void @call_void_intrinsic()
// OGCG:         call void @llvm.debugtrap()
// OGCG-NEXT:    ret void
