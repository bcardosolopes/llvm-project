// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// CIR coercion alloca for return value had alignment from the struct
// (align 2) instead of max(structAlign, prefTypeAlign(coerceType)) = align 8.
// This caused a load with align 8 from an alloca with align 2 (LLVM IR UB).

struct G { short x, y, z; };

// The return value coercion alloca (i48) must have alignment >= 8, matching
// the preferred alignment of i48 on x86-64.
struct G ret_g(int x, int y, int z) {
  return (struct G) { x, y, z };
}

// LLVM: define dso_local i48 @ret_g
// LLVM: alloca i48, i64 1, align 8
// LLVM: load i48, ptr %{{.*}}, align 8

// OGCG: define dso_local i48 @ret_g
// OGCG: alloca i48, align 8
// OGCG: load i48, ptr %{{.*}}, align 8
