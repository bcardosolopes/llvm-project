// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -mmlir --mlir-print-ir-before=cir-lowering-prepare %s -o %t.cir 2> %t-before-lp.cir
// RUN: FileCheck --input-file=%t-before-lp.cir %s -check-prefix=BEFORE-LP
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// An argument the x86-64 SysV ABI never puts in a register is always in the
// va_list overflow area, so va_arg on it needs no register save area path.
// LoweringPrepare asks the LLVM ABI Lowering Library which types those are and
// expands cir.va_arg into the overflow-area sequence itself. Handing the op to
// the generic llvm.va_arg instruction instead is not viable: no production
// frontend emits that instruction, and the x86 backend has no expansion for
// x86_fp80 at all.

typedef __builtin_va_list va_list;

// long double is X87/X87UP: MEMORY, 16-byte aligned, 16-byte slot.

long double get_long_double(va_list ap) {
  return __builtin_va_arg(ap, long double);
}

// BEFORE-LP-LABEL: cir.func {{.*}} @get_long_double(
// BEFORE-LP:   cir.va_arg %{{.+}} : (!cir.ptr<!rec___va_list_tag>) -> !cir.long_double<!cir.f80>

// CIR-LABEL: cir.func {{.*}} @get_long_double(
// CIR-NOT:   cir.va_arg
// CIR:   %[[AREA_P:.+]] = cir.get_member %{{.+}}[2] {name = "overflow_arg_area"} : !cir.ptr<!rec___va_list_tag> -> !cir.ptr<!cir.ptr<!void>>
// CIR:   %[[AREA:.+]] = cir.load align(8) %[[AREA_P]] : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
// CIR:   %[[BYTES:.+]] = cir.cast bitcast %[[AREA]] : !cir.ptr<!void> -> !cir.ptr<!u8i>
// CIR:   %[[BIAS:.+]] = cir.const #cir.int<15> : !u64i
// CIR:   %[[BIASED:.+]] = cir.ptr_stride %[[BYTES]], %[[BIAS]] : (!cir.ptr<!u8i>, !u64i) -> !cir.ptr<!u8i>
// CIR:   %[[MASK:.+]] = cir.const #cir.int<-16> : !s64i
// CIR:   %[[ALIGNED:.+]] = cir.call_llvm_intrinsic "ptrmask.p0.i64" %[[BIASED]], %[[MASK]] : (!cir.ptr<!u8i>, !s64i) -> !cir.ptr<!u8i>
// CIR:   %[[SIZE:.+]] = cir.const #cir.int<16> : !u64i
// CIR:   %[[NEXT:.+]] = cir.ptr_stride %[[ALIGNED]], %[[SIZE]] : (!cir.ptr<!u8i>, !u64i) -> !cir.ptr<!u8i>
// CIR:   %[[NEXT_V:.+]] = cir.cast bitcast %[[NEXT]] : !cir.ptr<!u8i> -> !cir.ptr<!void>
// CIR:   cir.store align(8) %[[NEXT_V]], %[[AREA_P]] : !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>
// CIR:   %[[SLOT:.+]] = cir.cast bitcast %[[ALIGNED]] : !cir.ptr<!u8i> -> !cir.ptr<!cir.long_double<!cir.f80>>
// CIR:   %{{.+}} = cir.load align(16) %[[SLOT]] : !cir.ptr<!cir.long_double<!cir.f80>>, !cir.long_double<!cir.f80>

// LLVM-LABEL: define {{.*}} x86_fp80 @get_long_double(
// LLVM:   %[[AREA_P:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.+}}, i32 0, i32 2
// LLVM:   %[[AREA:.+]] = load ptr, ptr %[[AREA_P]], align 8
// LLVM:   %[[BIASED:.+]] = getelementptr i8, ptr %[[AREA]], i64 15
// LLVM:   %[[ALIGNED:.+]] = call ptr @llvm.ptrmask.p0.i64(ptr %[[BIASED]], i64 -16)
// LLVM:   %[[NEXT:.+]] = getelementptr i8, ptr %[[ALIGNED]], i64 16
// LLVM:   store ptr %[[NEXT]], ptr %[[AREA_P]], align 8
// LLVM:   %{{.+}} = load x86_fp80, ptr %[[ALIGNED]], align 16

// OGCG-LABEL: define {{.*}} x86_fp80 @get_long_double(
// OGCG:   %[[AREA_P:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.+}}, i32 0, i32 2
// OGCG:   %[[AREA:.+]] = load ptr, ptr %[[AREA_P]], align 8
// OGCG:   %[[BIASED:.+]] = getelementptr inbounds i8, ptr %[[AREA]], i32 15
// OGCG:   %[[ALIGNED:.+]] = call ptr @llvm.ptrmask.p0.i64(ptr %[[BIASED]], i64 -16)
// OGCG:   %[[NEXT:.+]] = getelementptr i8, ptr %[[ALIGNED]], i32 16
// OGCG:   store ptr %[[NEXT]], ptr %[[AREA_P]], align 8
// OGCG:   %{{.+}} = load x86_fp80, ptr %[[ALIGNED]], align 16

// _Complex long double is COMPLEX_X87: same alignment, but a 32-byte slot.

_Complex long double get_complex_long_double(va_list ap) {
  return __builtin_va_arg(ap, _Complex long double);
}

// CIR-LABEL: cir.func {{.*}} @get_complex_long_double(
// CIR-NOT:   cir.va_arg
// CIR:   %[[MASK:.+]] = cir.const #cir.int<-16> : !s64i
// CIR:   %[[ALIGNED:.+]] = cir.call_llvm_intrinsic "ptrmask.p0.i64" %{{.+}}, %[[MASK]] : (!cir.ptr<!u8i>, !s64i) -> !cir.ptr<!u8i>
// CIR:   %[[SIZE:.+]] = cir.const #cir.int<32> : !u64i
// CIR:   %{{.+}} = cir.ptr_stride %[[ALIGNED]], %[[SIZE]] : (!cir.ptr<!u8i>, !u64i) -> !cir.ptr<!u8i>

// LLVM-LABEL: define {{.*}} { x86_fp80, x86_fp80 } @get_complex_long_double(
// LLVM:   %[[AREA_P:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.+}}, i32 0, i32 2
// LLVM:   %[[AREA:.+]] = load ptr, ptr %[[AREA_P]], align 8
// LLVM:   %[[BIASED:.+]] = getelementptr i8, ptr %[[AREA]], i64 15
// LLVM:   %[[ALIGNED:.+]] = call ptr @llvm.ptrmask.p0.i64(ptr %[[BIASED]], i64 -16)
// LLVM:   %[[NEXT:.+]] = getelementptr i8, ptr %[[ALIGNED]], i64 32
// LLVM:   store ptr %[[NEXT]], ptr %[[AREA_P]], align 8
// LLVM:   %{{.+}} = load { x86_fp80, x86_fp80 }, ptr %[[ALIGNED]], align 16

// OGCG-LABEL: define {{.*}} { x86_fp80, x86_fp80 } @get_complex_long_double(
// OGCG:   %[[AREA_P:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.+}}, i32 0, i32 2
// OGCG:   %[[AREA:.+]] = load ptr, ptr %[[AREA_P]], align 8
// OGCG:   %[[BIASED:.+]] = getelementptr inbounds i8, ptr %[[AREA]], i32 15
// OGCG:   %[[ALIGNED:.+]] = call ptr @llvm.ptrmask.p0.i64(ptr %[[BIASED]], i64 -16)
// OGCG:   %[[NEXT:.+]] = getelementptr i8, ptr %[[ALIGNED]], i32 32
// OGCG:   store ptr %[[NEXT]], ptr %[[AREA_P]], align 8
// OGCG:   %{{.+}} = getelementptr inbounds nuw { x86_fp80, x86_fp80 }, ptr %[[ALIGNED]], i32 0, i32 0

// A struct larger than two eightbytes is MEMORY as well, and it needs no more
// than the area's own 8-byte alignment, so the round-up is skipped and only
// the size is rounded up to a multiple of 8.

typedef struct {
  int a, b, c, d, e;
} Big;

Big get_big(va_list ap) { return __builtin_va_arg(ap, Big); }

// CIR-LABEL: cir.func {{.*}} @get_big(
// CIR-NOT:   cir.va_arg
// CIR-NOT:   cir.call_llvm_intrinsic "ptrmask.p0.i64"
// CIR:   %[[AREA_P:.+]] = cir.get_member %{{.+}}[2] {name = "overflow_arg_area"} : !cir.ptr<!rec___va_list_tag> -> !cir.ptr<!cir.ptr<!void>>
// CIR:   %[[AREA:.+]] = cir.load align(8) %[[AREA_P]] : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
// CIR:   %[[BYTES:.+]] = cir.cast bitcast %[[AREA]] : !cir.ptr<!void> -> !cir.ptr<!u8i>
// CIR:   %[[SIZE:.+]] = cir.const #cir.int<24> : !u64i
// CIR:   %[[NEXT:.+]] = cir.ptr_stride %[[BYTES]], %[[SIZE]] : (!cir.ptr<!u8i>, !u64i) -> !cir.ptr<!u8i>
// CIR:   %[[NEXT_V:.+]] = cir.cast bitcast %[[NEXT]] : !cir.ptr<!u8i> -> !cir.ptr<!void>
// CIR:   cir.store align(8) %[[NEXT_V]], %[[AREA_P]] : !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>
// CIR:   %[[SLOT:.+]] = cir.cast bitcast %[[BYTES]] : !cir.ptr<!u8i> -> !cir.ptr<!rec_Big>
// CIR:   %{{.+}} = cir.load align(4) %[[SLOT]] : !cir.ptr<!rec_Big>, !rec_Big

// LLVM-LABEL: @get_big(
// LLVM:   %[[AREA_P:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.+}}, i32 0, i32 2
// LLVM:   %[[AREA:.+]] = load ptr, ptr %[[AREA_P]], align 8
// LLVM:   %[[NEXT:.+]] = getelementptr i8, ptr %[[AREA]], i64 24
// LLVM:   store ptr %[[NEXT]], ptr %[[AREA_P]], align 8
// LLVM:   %{{.+}} = load %struct.Big, ptr %[[AREA]], align 4

// OGCG-LABEL: @get_big(
// OGCG:   %[[AREA_P:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.+}}, i32 0, i32 2
// OGCG:   %[[AREA:.+]] = load ptr, ptr %[[AREA_P]], align 8
// OGCG:   %[[NEXT:.+]] = getelementptr i8, ptr %[[AREA]], i32 24
// OGCG:   store ptr %[[NEXT]], ptr %[[AREA_P]], align 8
// OGCG:   call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.+}}, ptr align 4 %[[AREA]], i64 20, i1 false)

// A type the ABI can pass in registers still needs the register save area
// path, which is not implemented, so cir.va_arg survives LoweringPrepare.

double get_double(va_list ap) { return __builtin_va_arg(ap, double); }

// CIR-LABEL: cir.func {{.*}} @get_double(
// CIR:   cir.va_arg %{{.+}} : (!cir.ptr<!rec___va_list_tag>) -> !cir.double
