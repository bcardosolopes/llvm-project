// RUN: %clang_cc1 -triple x86_64-linux-gnu -O0 -disable-llvm-passes \
// RUN:   -fsanitize=null -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=NULL
// RUN: %clang_cc1 -triple x86_64-linux-gnu -O0 -disable-llvm-passes \
// RUN:   -fsanitize=null -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=NULL

// RUN: %clang_cc1 -triple x86_64-linux-gnu -O0 -disable-llvm-passes \
// RUN:   -fsanitize=null,alignment -fclangir -emit-llvm %s -o %t2.cir.ll
// RUN: FileCheck --input-file=%t2.cir.ll %s --check-prefix=ALIGN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -O0 -disable-llvm-passes \
// RUN:   -fsanitize=null,alignment -emit-llvm %s -o %t2.ogcg.ll
// RUN: FileCheck --input-file=%t2.ogcg.ll %s --check-prefix=ALIGN

// Verify LogAlignment in TypeMismatchData matches OGCG.
// With -fsanitize=null only (no alignment check), LogAlignment should be 1
// (the OGCG fallback value). With -fsanitize=null,alignment, it should be
// log2(4)=2 for int* dereference.

int test(int *p) { return *p; }

// NULL: @{{[0-9]+}} = private unnamed_addr global {{.*}} i8 1, i8 0
// ALIGN: @{{[0-9]+}} = private unnamed_addr global {{.*}} i8 2, i8 0
