// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fwrapv-pointer %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fwrapv-pointer %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// With -fwrapv-pointer, struct member GEPs should not use inbounds/nuw.

struct S { int a; int b; };

int test(struct S *s) { return s->b; }

// CHECK: getelementptr %struct.S, ptr %{{.*}}, i32 0, i32 1
// CHECK-NOT: inbounds

// OGCG: getelementptr %struct.S, ptr %{{.*}}, i32 0, i32 1
// OGCG-NOT: inbounds
