// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR

// Verify that struct field annotations emit llvm.ptr.annotation calls.

struct foo {
    int v __attribute__((annotate("v_ann_0"))) __attribute__((annotate("v_ann_1")));
    int w;
};

int read_annotated(struct foo *f) {
    return f->v;
}

// Two annotations on field v: two llvm.ptr.annotation calls.
// LLVM-LABEL: define {{.*}} @read_annotated
// LLVM: getelementptr inbounds nuw %struct.foo, ptr %{{.*}}, i32 0, i32 0
// LLVM: call ptr @llvm.ptr.annotation.p0.p0(ptr %{{.*}}, ptr @{{.*}}, ptr @{{.*}}, i32 8, ptr null)
// LLVM: call ptr @llvm.ptr.annotation.p0.p0(ptr %{{.*}}, ptr @{{.*}}, ptr @{{.*}}, i32 8, ptr null)
// LLVM: load i32, ptr %{{.*}}
// OGCG-LABEL: define {{.*}} @read_annotated
// OGCG: getelementptr inbounds nuw %struct.foo, ptr %{{.*}}, i32 0, i32 0
// OGCG: call ptr @llvm.ptr.annotation.p0.p0(ptr %{{.*}}, ptr @{{.*}}, ptr @{{.*}}, i32 8, ptr null)
// OGCG: call ptr @llvm.ptr.annotation.p0.p0(ptr %{{.*}}, ptr @{{.*}}, ptr @{{.*}}, i32 8, ptr null)
// OGCG: load i32, ptr %{{.*}}

int read_unannotated(struct foo *f) {
    return f->w;
}

// No annotation on field w: no llvm.ptr.annotation calls.
// LLVM-LABEL: define {{.*}} @read_unannotated
// LLVM: getelementptr inbounds nuw %struct.foo, ptr %{{.*}}, i32 0, i32 1
// LLVM-NOT: llvm.ptr.annotation
// LLVM: load i32, ptr %{{.*}}
// OGCG-LABEL: define {{.*}} @read_unannotated
// OGCG: getelementptr inbounds nuw %struct.foo, ptr %{{.*}}, i32 0, i32 1
// OGCG-NOT: llvm.ptr.annotation
// OGCG: load i32, ptr %{{.*}}

// CIR-level check: verify cir.ptr_annotation is emitted.
// CIR: cir.ptr_annotation({{%.*}}, [#cir.annotation<name = "v_ann_0", args = []>, #cir.annotation<name = "v_ann_1", args = []>]) : !cir.ptr<!s32i>
