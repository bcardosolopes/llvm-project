// RUN: %clang_cc1 -triple arm64_32-apple-ios7.0 -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple arm64_32-apple-ios7.0 -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

typedef float __attribute__((ext_vector_type(16))) v16f32;

v16f32 func(v16f32 in) { return in; }

// LLVM-LABEL: define void @func(
// LLVM-SAME: ptr dead_on_unwind noalias writable sret(<16 x float>) align 16
// LLVM: %[[TMP:.+]] = alloca <16 x float>, i64 1, align 16
// LLVM: store <16 x float> %{{.+}}, ptr %[[TMP]], align 16
// LLVM: %[[VAL:.+]] = load <16 x float>, ptr %[[TMP]], align 16
// LLVM: store <16 x float> %[[VAL]], ptr %{{.+}}, align 16

// OGCG-LABEL: define void @func(
// OGCG-SAME: ptr dead_on_unwind noalias writable sret(<16 x float>) align 16
// OGCG: %[[TMP:.+]] = alloca <16 x float>, align 16
// OGCG: store <16 x float> %{{.+}}, ptr %[[TMP]], align 16
// OGCG: %[[VAL:.+]] = load <16 x float>, ptr %[[TMP]], align 16
// OGCG: store <16 x float> %[[VAL]], ptr %{{.+}}, align 16
