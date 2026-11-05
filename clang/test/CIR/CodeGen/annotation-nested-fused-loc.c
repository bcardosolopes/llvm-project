// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -fclangir -emit-llvm \
// RUN:   -Oz -debug-info-kind=limited -std=gnu11 -w %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify that annotated local variables compile correctly when key instruction
// debug info produces nested FusedLoc metadata. The annotation lowering must
// unwrap all FusedLoc layers to extract the file/line/column location.

#define ANNOTATED(name, val) \
  int name __attribute__((annotate("mytest"))); \
  name = (val)

int bar(int);

void foo(int a) {
  ANNOTATED(x, bar(a));
  ANNOTATED(y, bar(x));
}

// CHECK: @.str.annot = private unnamed_addr constant [7 x i8] c"mytest\00"
// CHECK-LABEL: define {{.*}}void @foo(
// CHECK: call void @llvm.var.annotation.p0.p0(ptr {{.*}}, ptr {{.*}}@.str.annot
// CHECK: call void @llvm.var.annotation.p0.p0(ptr {{.*}}, ptr {{.*}}@.str.annot
