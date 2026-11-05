// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.cir
// RUN: FileCheck --check-prefix=FLAT %s --input-file=%t.cir
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.ll
// RUN: %clang_cc1 -triple aarch64-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.og.ll

// On AArch64, _Atomic structs that fit in registers are passed directly,
// not via sret/byval. Verify CIR matches OGCG for small atomic structs.

struct S4 { int x; };
struct S8 { int x; int y; };

_Atomic struct S4 load4(_Atomic struct S4 x) { return x; }
_Atomic struct S8 load8(_Atomic struct S8 x) { return x; }

// FLAT-LABEL: cir.func{{.*}} @load4(
// FLAT-NOT: ["atomic-temp"]
// FLAT-NOT: cir.copy
// FLAT: %[[LOAD4:.*]] = cir.load align(4) atomic(seq_cst) %{{.*}} : !cir.ptr<!u32i>, !u32i
// FLAT: cir.store align(4) %[[LOAD4]], %{{.*}} : !u32i, !cir.ptr<!u32i>
// FLAT-NOT: ["atomic-temp"]
// FLAT-NOT: cir.copy
//
// FLAT-LABEL: cir.func{{.*}} @load8(
// FLAT-NOT: ["atomic-temp"]
// FLAT-NOT: cir.copy
// FLAT: %[[LOAD8:.*]] = cir.load align(8) atomic(seq_cst) %{{.*}} : !cir.ptr<!u64i>, !u64i
// FLAT: cir.store align(8) %[[LOAD8]], %{{.*}} : !u64i, !cir.ptr<!u64i>
// FLAT-NOT: ["atomic-temp"]
// FLAT-NOT: cir.copy
//
// CIR-LABEL: define dso_local i32 @load4(i64 %{{.*}})
// CIR: load atomic i32, ptr %{{.*}} seq_cst, align 4
// CIR-NOT: @llvm.memcpy
// CIR: ret i32 %{{.*}}
//
// CIR-LABEL: define dso_local i64 @load8(i64 %{{.*}})
// CIR: load atomic i64, ptr %{{.*}} seq_cst, align 8
// CIR-NOT: @llvm.memcpy
// CIR: ret i64 %{{.*}}

// OGCG-LABEL: define dso_local i32 @load4(i64 %{{.*}})
// OGCG: load atomic i32, ptr %{{.*}} seq_cst, align 4
// OGCG-NOT: @llvm.memcpy
// OGCG: ret i32 %{{.*}}
//
// OGCG-LABEL: define dso_local i64 @load8(i64 %{{.*}})
// OGCG: load atomic i64, ptr %{{.*}} seq_cst, align 8
// OGCG-NOT: @llvm.memcpy
// OGCG: ret i64 %{{.*}}
