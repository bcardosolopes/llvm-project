// RUN: %clang_cc1 -triple x86_64-apple-darwin -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll %s

// Verify that struct copy-assignment with volatile members uses a volatile
// memcpy (isVolatile=true) instead of field-by-field copy.

struct s {
  char filler[128];
  volatile int x;
};

struct s gs;

void foo(void) {
  struct s ls;
  ls = ls;
  gs = gs;
  ls = gs;
}
// CHECK-LABEL: define{{.*}} void @_Z3foov()
// CHECK: %[[LS:.*]] = alloca %struct.s,
// CHECK: call void @llvm.memcpy.{{.*}}(ptr align 4 %[[LS]], ptr align 4 %[[LS]], i64 132, i1 true)
// CHECK: call void @llvm.memcpy.{{.*}}(ptr align 4 @gs, ptr align 4 @gs, i64 132, i1 true)
// CHECK: call void @llvm.memcpy.{{.*}}(ptr align 4 %[[LS]], ptr align 4 @gs, i64 132, i1 true)

struct s1 {
  struct s y;
};

struct s1 s;

void fee(void) {
  s = s;
  s.y = gs;
}
// CHECK-LABEL: define{{.*}} void @_Z3feev()
// CHECK: call void @llvm.memcpy.{{.*}}(ptr align 4 @s, ptr align 4 @s, i64 132, i1 true)
// CHECK: call void @llvm.memcpy.{{.*}}(ptr align 4 @s, ptr align 4 @gs, i64 132, i1 true)

struct d : s1 {};

d gd;

void gorf(void) {
  gd = gd;
}
// CHECK-LABEL: define{{.*}} void @_Z4gorfv()
// CHECK: call void @llvm.memcpy.{{.*}}(ptr align 4 @gd, ptr align 4 @gd, i64 132, i1 true)
