// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG
//
// 256-bit vectors should be passed indirectly (byval) on x86_64
// without AVX, since SSE2 only has 128-bit registers.

typedef unsigned long long uint64_t4 __attribute__((ext_vector_type(4)));

// CIR-LABEL: define {{.*}} @_Z20ManglingTestRetParamDv4_y
// CIR-SAME: ptr noundef byval(<4 x i64>) align 32 %

// OGCG-LABEL: define {{.*}} @_Z20ManglingTestRetParamDv4_y
// OGCG-SAME: ptr noundef byval(<4 x i64>) align 32 %
uint64_t4 ManglingTestRetParam(uint64_t4 Param) { return Param; }
