// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s -check-prefix=OGCG

int puts(const char *);

// Static local variable with variably modified type (pointer to VLA).
// The VLA bound must be evaluated at runtime even though the variable is static.
int static_vla_ptr(void) {
  static int (*y)[puts("asdf")];
  return sizeof(*y);
}

// CIR: cir.func {{.*}} @static_vla_ptr()
// CIR:   cir.call @puts(
// CIR:   cir.cast integral %{{.*}} : !s32i -> !u32i
// CIR:   cir.cast integral %{{.*}} : !u32i -> !u64i
// CIR:   cir.return

// LLVM: define {{.*}} @static_vla_ptr()
// LLVM:   call {{.*}} @puts(

// OGCG: define {{.*}} @static_vla_ptr()
// OGCG:   call {{.*}} @puts(
