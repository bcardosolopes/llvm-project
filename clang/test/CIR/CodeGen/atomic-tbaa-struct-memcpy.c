// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O2 -disable-llvm-passes %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIRLLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O2 -disable-llvm-passes %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

struct S3 {
  char a, b, c;
};

void t(struct S3 *in, struct S3 *out) {
  _Atomic(struct S3) a;
  __c11_atomic_store(&a, *in, __ATOMIC_SEQ_CST);
  *out = __c11_atomic_load(&a, __ATOMIC_SEQ_CST);
}

// CIRLLVM-LABEL: define{{.*}} void @t(
// CIRLLVM: call void @llvm.memcpy{{.*}}i64(ptr align 1 {{.*}}, ptr align 1 {{.*}}, i64 3, i1 false), !tbaa.struct ![[TS:[0-9]+]]
// CIRLLVM: call void @llvm.memcpy{{.*}}i64(ptr align 1 {{.*}}, ptr align 4 {{.*}}, i64 3, i1 false), !tbaa.struct ![[TS]]

// OGCG-LABEL: define{{.*}} void @t(
// OGCG: call void @llvm.memcpy{{.*}}i64(ptr align 1 {{.*}}, ptr align 1 {{.*}}, i64 3, i1 false), !tbaa.struct ![[TS:[0-9]+]]
// OGCG: call void @llvm.memcpy{{.*}}i64(ptr align 1 {{.*}}, ptr align 4 {{.*}}, i64 3, i1 false), !tbaa.struct ![[TS]]
