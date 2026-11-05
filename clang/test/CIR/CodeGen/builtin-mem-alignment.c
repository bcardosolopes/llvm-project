// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// __builtin_memset, __builtin_memmove, and bzero must propagate
// pointer alignment to llvm.memset / llvm.memmove intrinsics.

struct x { int a[100]; };

void test_memset(struct x *P) {
  __builtin_memset(P, 0, sizeof(*P));
}

// CIR: call void @llvm.memset.p0.i64(ptr align 4 %{{.*}}, i8 0, i64 400, i1 false)
// OGCG: call void @llvm.memset.p0.i64(ptr align 4 %{{.*}}, i8 0, i64 400, i1 false)

void test_memmove(struct x *P, struct x *Q) {
  __builtin_memmove(P, Q, sizeof(*P));
}

// CIR: call void @llvm.memmove.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 400, i1 false)
// OGCG: call void @llvm.memmove.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 400, i1 false)

void test_bzero(struct x *P) {
  __builtin_bzero(P, sizeof(*P));
}

// CIR: call void @llvm.memset.p0.i64(ptr align 4 %{{.*}}, i8 0, i64 400, i1 false)
// OGCG: call void @llvm.memset.p0.i64(ptr align 4 %{{.*}}, i8 0, i64 400, i1 false)
