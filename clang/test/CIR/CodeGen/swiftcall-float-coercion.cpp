// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG
//
// Swiftcall ABI lowering should match OGCG and keep consecutive
// float fields as separate scalar parameters.
//
// OGCG and CIR both lower arguments as (i64, float, float).
// Both return and argument types now use CoerceAndExpand, matching OGCG.

struct S { long a; float b; float c; };

// CIR-LABEL: define {{.*}}swiftcc { i64, float, float } @_Z15return_struct_1v

// OGCG-LABEL: define {{.*}}swiftcc {{.*}} @_Z15return_struct_1v
// OGCG: { i64, float, float }
S __attribute__((swiftcall)) return_struct_1() { return {1, 2.0f, 3.0f}; }

// CIR-LABEL: define {{.*}}swiftcc void @_Z13take_struct_11S
// CIR-SAME: i64 %{{[0-9]+}}, float %{{[0-9]+}}, float %{{[0-9]+}}

// OGCG-LABEL: define {{.*}}swiftcc void @_Z13take_struct_11S
// OGCG-SAME: i64 %{{[0-9]+}}, float %{{[0-9]+}}, float %{{[0-9]+}}
void __attribute__((swiftcall)) take_struct_1(S s) {}
