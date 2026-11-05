// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify FP16 pair and mixed FP type SSE coercion on x86-64.

struct FP16Pair { _Float16 a, b; };
struct MixedFP { _Float16 h; float f; };

// CHECK: <2 x half> @_Z13make_fp16pair{{.*}}()
// OGCG: <2 x half> @_Z13make_fp16pair{{.*}}()
FP16Pair make_fp16pair() { return {(_Float16)1.0f, (_Float16)2.0f}; }

// CHECK: <4 x half> @_Z10make_mixed{{.*}}()
// OGCG: <4 x half> @_Z10make_mixed{{.*}}()
MixedFP make_mixed() { return {(_Float16)1.0f, 2.0f}; }
