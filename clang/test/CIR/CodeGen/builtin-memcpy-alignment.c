// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// __builtin_memcpy propagates pointer alignment to llvm.memcpy

struct x { int a[100]; };

void foo(struct x *P, struct x *Q) {
  *P = *Q;
}

void bar(struct x *P, struct x *Q) {
  __builtin_memcpy(P, Q, sizeof(struct x));
}

// Struct assignment correctly has alignment:
// CIR-LABEL: @foo
// CIR: call void @llvm.memcpy{{.*}}(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}

// OGCG-LABEL: @foo
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}

// __builtin_memcpy should also have alignment:
// CIR-LABEL: @bar
// CIR: call void @llvm.memcpy{{.*}}(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}

// OGCG-LABEL: @bar
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}
