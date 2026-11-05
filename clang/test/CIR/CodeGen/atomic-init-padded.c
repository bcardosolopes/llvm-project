// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test __c11_atomic_init for padded aggregate types.
// A 6-byte struct {short, short, short} is padded to 8 bytes for atomic ops.
// emitMemSetZeroIfNecessary must zero-fill the padding before initialization,
// and projectValue must GEP into field 0 to access the value type.

typedef struct { short x, y, z; } PS;

// CIR-LABEL: cir.func {{.*}}@test_atomic_init_promoted(
// CIR:         cir.cast bitcast %{{.*}} : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!void>
// CIR:         cir.libc.memset
// CIR:         cir.get_member %{{.*}}[0] {name = ""} : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!rec_PS>
// CIR:         cir.get_member %{{.*}}[0] {name = "x"}
// CIR:         cir.store
// CIR:         cir.get_member %{{.*}}[1] {name = "y"}
// CIR:         cir.store
// CIR:         cir.get_member %{{.*}}[2] {name = "z"}
// CIR:         cir.store

// LLVM-LABEL: define {{.*}}@test_atomic_init_promoted(
// LLVM:         call void @llvm.memset.p0.i64(ptr align 8 %{{.*}}, i8 0, i64 8, i1 false)
// LLVM:         getelementptr inbounds nuw { %struct.PS, [2 x i8] }, ptr %{{.*}}, i32 0, i32 0
// LLVM:         store i16 1
// LLVM:         store i16 2
// LLVM:         store i16 3
void test_atomic_init_promoted(_Atomic(PS) *fp) {
  __c11_atomic_init(fp, (PS){1,2,3});
}

// Test local variable init with padded atomic.
// For locals, CIR emits a zero constant store instead of memset.
// CIR-LABEL: cir.func {{.*}}@test_atomic_local_init(
// CIR:         cir.const #cir.zero : !rec_anon_struct
// CIR:         cir.store
// CIR:         cir.get_member %{{.*}}[0] {name = ""} : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!rec_PS>

// LLVM-LABEL: define {{.*}}@test_atomic_local_init(
// LLVM:         store { %struct.PS, [2 x i8] } zeroinitializer
// LLVM:         getelementptr inbounds nuw { %struct.PS, [2 x i8] }
void test_atomic_local_init(void) {
  _Atomic(PS) x = (PS){4,5,6};
}

// Test local padded atomic init where some explicit elements are simple zero.
// Since the destination is already zeroed, CIR should skip redundant
// per-field stores of zero and only emit stores for non-zero elements.
// CIR-LABEL: cir.func {{.*}}@test_atomic_local_init_skip_zero_stores(
// CIR:         cir.const #cir.zero : !rec_anon_struct
// CIR:         cir.store
// CIR:         %[[PS:.*]] = cir.get_member %{{.*}}[0] {name = ""} : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!rec_PS>
// CIR-NEXT:    %[[XADDR:.*]] = cir.get_member %[[PS]][0] {name = "x"} : !cir.ptr<!rec_PS> -> !cir.ptr<!s16i>
// CIR-NEXT:    %[[YADDR:.*]] = cir.get_member %[[PS]][1] {name = "y"} : !cir.ptr<!rec_PS> -> !cir.ptr<!s16i>
// CIR-NEXT:    %[[YVAL:.*]] = cir.const #cir.int<5> : !s16i
// CIR-NEXT:    cir.store align(2) %[[YVAL]], %[[YADDR]] : !s16i, !cir.ptr<!s16i>
// CIR-NEXT:    %[[ZADDR:.*]] = cir.get_member %[[PS]][2] {name = "z"} : !cir.ptr<!rec_PS> -> !cir.ptr<!s16i>
// CIR-NEXT:    cir.return

// LLVM-LABEL: define {{.*}}@test_atomic_local_init_skip_zero_stores(
// LLVM:         store { %struct.PS, [2 x i8] } zeroinitializer
// LLVM:         store i16 5
// LLVM-NOT:     store i16 0
void test_atomic_local_init_skip_zero_stores(void) {
  _Atomic(PS) x = (PS){0,5,0};
}
