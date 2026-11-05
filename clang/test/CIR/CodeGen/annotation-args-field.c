// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Verify that annotation args on field annotations are properly lowered
// to a constant struct global, matching OGCG.

struct S {
  int x __attribute__((annotate("field_anno", "extra_info", 42)));
};

void test(struct S *s) {
  s->x = 1;
}

// CIR preserves typed annotation args in ptr_annotation.
// CIR: cir.ptr_annotation
// CIR-SAME: #cir.annotation<name = "field_anno", args = ["extra_info", #cir.int<42> : !s32i]>

// CIR now lowers field annotation args to a dedicated args struct global,
// matching OGCG behavior.
// LLVM: call ptr @llvm.ptr.annotation.p0.p0(ptr %{{.*}}, ptr @{{.*}}, ptr @{{.*}}, i32 {{.*}}, ptr @{{.*}}args

// OGCG: call ptr @llvm.ptr.annotation.p0.p0(ptr %{{.*}}, ptr @{{.*}}, ptr @{{.*}}, i32 {{.*}}, ptr @{{.*}}args
