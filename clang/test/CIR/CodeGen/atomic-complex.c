// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test atomic operations on _Complex float types.
// Verifies CK_AtomicToNonAtomic / CK_NonAtomicToAtomic casts and
// atomic load/store of complex values go through integer conversion.

_Atomic(_Complex float) g;

// CIR: cir.func {{.*}}@read_global()
// CIR:   %[[ADDR:.*]] = cir.get_global @g
// CIR:   %[[INTADDR:.*]] = cir.cast bitcast %[[ADDR]] : !cir.ptr<!cir.complex<!cir.float>> -> !cir.ptr<!u64i>
// CIR:   %[[INT:.*]] = cir.load align(8) atomic(seq_cst) %[[INTADDR]] : !cir.ptr<!u64i>, !u64i
// CIR:   cir.store align(8) %[[INT]], %{{.*}} : !u64i, !cir.ptr<!u64i>
// CIR:   %[[CMPLX:.*]] = cir.load {{.*}} %{{.*}} : !cir.ptr<!cir.complex<!cir.float>>, !cir.complex<!cir.float>
// LLVM: define {{.*}}@read_global()
// LLVM:   %[[LINT:.*]] = load atomic i64, ptr @g seq_cst, align 8
// LLVM:   store i64 %[[LINT]], ptr %{{.*}}, align 8
// LLVM:   %[[LCMPLX:.*]] = load { float, float }, ptr %{{.*}}, align 8
_Complex float read_global(void) {
  return g;
}

// CIR: cir.func {{.*}}@write_global
// CIR:   cir.store {{.*}} %{{.*}} : !cir.complex<!cir.float>, !cir.ptr<!cir.complex<!cir.float>>
// CIR:   %[[INTADDR2:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!cir.complex<!cir.float>> -> !cir.ptr<!u64i>
// CIR:   %[[INT2:.*]] = cir.load {{.*}} %[[INTADDR2]] : !cir.ptr<!u64i>, !u64i
// CIR:   %[[DSTINT:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!cir.complex<!cir.float>> -> !cir.ptr<!u64i>
// CIR:   cir.store align(8) atomic(seq_cst) %[[INT2]], %[[DSTINT]] : !u64i, !cir.ptr<!u64i>
// LLVM: define {{.*}}@write_global
// LLVM:   %[[SINT:.*]] = load i64, ptr %{{.*}}, align 8
// LLVM:   store atomic i64 %[[SINT]], ptr @g seq_cst, align 8
void write_global(_Complex float val) {
  g = val;
}

// CIR: cir.func {{.*}}@local_init_and_read()
// CIR:   cir.store align(8) %{{.*}} : !cir.complex<!cir.float>
// CIR:   cir.cast bitcast %{{.*}} : !cir.ptr<!cir.complex<!cir.float>> -> !cir.ptr<!u64i>
// CIR:   cir.load align(8) atomic(seq_cst) %{{.*}} : !cir.ptr<!u64i>, !u64i
// LLVM: define {{.*}}@local_init_and_read()
// LLVM:   store { float, float } { float 1.000000e+00, float 2.000000e+00 }, ptr %{{.*}}, align 8
// LLVM:   %[[LI:.*]] = load atomic i64, ptr %{{.*}} seq_cst, align 8
_Complex float local_init_and_read(void) {
  _Atomic(_Complex float) local = 1.0f + 2.0fi;
  return local;
}

// Complex compound assignment on _Atomic type: strips the atomic wrapper
// for type routing, then uses atomic load/store for the operation.
//
// CIR: cir.func {{.*}}@compound_add_complex
// CIR:   cir.load align(8) atomic(seq_cst) %{{.*}} : !cir.ptr<!u64i>, !u64i
// CIR:   cir.complex.real
// CIR:   cir.complex.imag
// CIR:   cir.binop(add
// CIR:   cir.complex.create
// CIR:   cir.store align(8) atomic(seq_cst) %{{.*}}, %{{.*}} : !u64i, !cir.ptr<!u64i>
// LLVM: define {{.*}}@compound_add_complex
// LLVM:   load atomic i64, ptr %{{.*}} seq_cst, align 8
// LLVM:   fadd float
// LLVM:   store atomic i64 %{{.*}}, ptr %{{.*}} seq_cst, align 8
void compound_add_complex(_Atomic(_Complex float) *p) {
  *p += 1.0f;
}
