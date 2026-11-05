// See also clang/test/CodeGen/AArch64/varargs.c (empty record test)
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck %s --input-file=%t.cir.ll --check-prefix=CHECK
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=CHECK

// Test that empty struct return/argument types on AArch64 are correctly
// lowered to void/ignored, matching OGCG behavior.

typedef struct {} empty;

// CHECK: define {{.*}}void @ret_empty()
empty ret_empty(void) { empty e; return e; }

// CHECK: define {{.*}}void @arg_empty()
void arg_empty(empty e) {}

// CHECK: define {{.*}}i32 @mixed_args(i32 noundef %{{.*}})
int mixed_args(empty e, int x) { return x; }

// Small non-empty struct for comparison — should be coerced to i32 direct.
typedef struct { int a; } small;

// CHECK: define {{.*}}i32 @ret_small()
small ret_small(void) { small s = {42}; return s; }
