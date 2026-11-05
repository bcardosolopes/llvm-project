// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

extern int target_fn(int);

#define DECL_FN(ID) extern int decl_fn_##ID(int);
DECL_FN(00)
DECL_FN(01)
DECL_FN(02)
DECL_FN(03)
DECL_FN(04)
DECL_FN(05)
DECL_FN(06)
DECL_FN(07)
DECL_FN(08)
DECL_FN(09)
DECL_FN(10)
DECL_FN(11)
DECL_FN(12)
DECL_FN(13)
DECL_FN(14)
DECL_FN(15)
DECL_FN(16)
DECL_FN(17)
DECL_FN(18)
DECL_FN(19)
DECL_FN(20)
DECL_FN(21)
DECL_FN(22)
DECL_FN(23)
DECL_FN(24)
DECL_FN(25)
DECL_FN(26)
DECL_FN(27)
DECL_FN(28)
DECL_FN(29)

int many_decls_still_lowers(int x) { return target_fn(x); }

// CHECK-LABEL: define{{.*}} i32 @many_decls_still_lowers(
// CHECK: call i32 @target_fn(i32
