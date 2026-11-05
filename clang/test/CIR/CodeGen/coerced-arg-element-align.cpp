// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-call-conv-lowering -emit-mlir=cir-abi -std=c++20 -w %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-call-conv-lowering -emit-llvm -std=c++20 -w %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that lowered coerced aggregate argument stores keep OGCG-equivalent
// per-element alignment. This path was refactored to avoid repeated
// RecordType::getElementOffset prefix rescans in element loops.

struct alignas(16) Pair {
  long long a;
  int b;
  short c;
};

long long callee(Pair);

// CIR-LABEL: cir.func no_inline optnone dso_local @_Z6caller4Pair(
// CIR: %[[SRC:.+]] = cir.alloca !rec_Pair, !cir.ptr<!rec_Pair>, [""] {alignment = 16 : i64}
// CIR: %[[AGG:.+]] = cir.alloca !rec_Pair, !cir.ptr<!rec_Pair>, ["agg.tmp0"] {alignment = 16 : i64}
// CIR: cir.copy %[[SRC]] to %[[AGG]] {dst_alignment = 16 : i64, src_alignment = 16 : i64} : !cir.ptr<!rec_Pair>
// CIR-NOT: ["tmp"] {alignment = 8 : i64}
// CIR: %[[CAST:.+]] = cir.cast bitcast %[[AGG]] : !cir.ptr<!rec_Pair> -> !cir.ptr<!rec_anon_struct>
// CIR: %[[M0:.+]] = cir.get_member %[[CAST]][0] {name = ""} : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!s64i>
// CIR: %[[ARG0:.+]] = cir.load align(16) %[[M0]] : !cir.ptr<!s64i>, !s64i
// CIR: %[[M1:.+]] = cir.get_member %[[CAST]][1] {name = ""} : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!u64i>
// CIR: %[[ARG1:.+]] = cir.load align(8) %[[M1]] : !cir.ptr<!u64i>, !u64i
// CIR: %[[RET:.+]] = cir.call @_Z6callee4Pair(%[[ARG0]], %[[ARG1]]) : (!s64i, !u64i) -> !s64i

// LLVM-LABEL: define {{.*}} @_Z6caller4Pair(i64 %0, i64 %1)
// LLVM: %[[SRC:.+]] = alloca %struct.Pair, i64 1, align 16
// LLVM: %[[G0:.+]] = getelementptr inbounds nuw { i64, i64 }, ptr %[[SRC]], i32 0, i32 0
// LLVM: store i64 %0, ptr %[[G0]], align 16
// LLVM: %[[G1:.+]] = getelementptr inbounds nuw { i64, i64 }, ptr %[[SRC]], i32 0, i32 1
// LLVM: store i64 %1, ptr %[[G1]], align 8
// LLVM: %[[AGG:.+]] = alloca %struct.Pair, i64 1, align 16
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 16 %[[AGG]], ptr align 16 %[[SRC]], i64 16, i1 false)
// LLVM: %[[ARGP0:.+]] = getelementptr inbounds nuw { i64, i64 }, ptr %[[AGG]], i32 0, i32 0
// LLVM: %[[ARG0:.+]] = load i64, ptr %[[ARGP0]], align 16
// LLVM: %[[ARGP1:.+]] = getelementptr inbounds nuw { i64, i64 }, ptr %[[AGG]], i32 0, i32 1
// LLVM: %[[ARG1:.+]] = load i64, ptr %[[ARGP1]], align 8
// LLVM: %[[RET:.+]] = call noundef i64 @_Z6callee4Pair(i64 %[[ARG0]], i64 %[[ARG1]])
long long caller(Pair p) { return callee(p); }
