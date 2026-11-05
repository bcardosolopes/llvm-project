// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --check-prefix=CIR < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: FileCheck %s --check-prefix=LLVMATTR < %t.ll

// Test that atomic unary ++/-- on _Atomic long double (x86_fp80) emits
// a CAS loop using __atomic_load / __atomic_compare_exchange library calls.
//
//

// Pre-increment: ++n (returns new value).
long double test_pre_inc(_Atomic long double *p) {
  return ++*p;
}

// CIR-LABEL: @test_pre_inc
// CIR:         cir.call @__atomic_load
// CIR:         cir.do {
// CIR:           cir.binop(add,
// CIR:           cir.atomic.cmpxchg
// CIR:           cir.yield
// CIR-NEXT:    } while {
// CIR:           cir.condition
// CIR-NEXT:    }

// LLVM-LABEL: @test_pre_inc
// LLVM:         call void @__atomic_load(i64 noundef 16,
// LLVM:         fadd x86_fp80 {{.*}}, 0xK3FFF8000000000000000
// LLVM:         call void @llvm.memset
// LLVM:         call void @llvm.memset
// LLVM:         call zeroext i1 @__atomic_compare_exchange(i64 noundef 16,

// Post-decrement: n-- (returns old value).
long double test_post_dec(_Atomic long double *p) {
  return (*p)--;
}

// CIR-LABEL: @test_post_dec
// CIR:         %[[NEG_ONE:.*]] = cir.const #cir.fp<-1.000000e+00> : !cir.long_double<!cir.f80>
// CIR:         cir.call @__atomic_load
// CIR:         cir.do {
// CIR:           cir.binop(add,
// CIR:           cir.atomic.cmpxchg
// CIR:           cir.yield
// CIR-NEXT:    } while {
// CIR:           cir.condition
// CIR-NEXT:    }

// LLVM-LABEL: @test_post_dec
// LLVM:         call void @__atomic_load(i64 noundef 16,
// LLVM:         fadd x86_fp80 {{.*}}, 0xKBFFF8000000000000000
// LLVM:         call void @llvm.memset
// LLVM:         call void @llvm.memset
// LLVM:         call zeroext i1 @__atomic_compare_exchange(i64 noundef 16,

void test_store(_Atomic long double *p, long double x) {
  *p = x;
}

// CIR-LABEL: @test_store
// CIR:         %[[TMP:.*]] = cir.alloca !cir.long_double<!cir.f80>, !cir.ptr<!cir.long_double<!cir.f80>>, ["atomic-temp"]
// CIR:         %[[X:.*]] = cir.load align(16) %{{.*}} : !cir.ptr<!cir.long_double<!cir.f80>>, !cir.long_double<!cir.f80>
// CIR:         %[[P:.*]] = cir.load {{.*}} : !cir.ptr<!cir.ptr<!cir.long_double<!cir.f80>>>, !cir.ptr<!cir.long_double<!cir.f80>>
// CIR:         %[[TMPVOID:.*]] = cir.cast bitcast %[[TMP]] : !cir.ptr<!cir.long_double<!cir.f80>> -> !cir.ptr<!void>
// CIR:         cir.libc.memset {{.*}} from %[[TMPVOID]]
// CIR:         cir.store align(16) %[[X]], %[[TMP]]
// CIR:         %[[PVOID:.*]] = cir.cast bitcast %[[P]] : !cir.ptr<!cir.long_double<!cir.f80>> -> !cir.ptr<!void>
// CIR:         %[[CALLTMP:.*]] = cir.cast bitcast %[[TMP]] : !cir.ptr<!cir.long_double<!cir.f80>> -> !cir.ptr<!void>
// CIR:         cir.call @__atomic_store({{.*}}, %[[PVOID]], %[[CALLTMP]], {{.*}})

// LLVM-LABEL: @test_store
// LLVM:         %[[PADDR:[0-9]+]] = alloca ptr, i64 1, align 8
// LLVM:         %[[XADDR:[0-9]+]] = alloca x86_fp80, i64 1, align 16
// LLVM:         %[[TMP:[0-9]+]] = alloca x86_fp80, i64 1, align 16
// LLVM:         store ptr %0, ptr %[[PADDR]], align 8
// LLVM:         store x86_fp80 %1, ptr %[[XADDR]], align 16
// LLVM:         %[[X:[0-9]+]] = load x86_fp80, ptr %[[XADDR]], align 16
// LLVM:         %[[P:[0-9]+]] = load ptr, ptr %[[PADDR]], align 8
// LLVM:         call void @llvm.memset.p0.i64(ptr align 16 %[[TMP]], i8 0, i64 16, i1 false)
// LLVM:         store x86_fp80 %[[X]], ptr %[[TMP]], align 16
// LLVM:         call void @__atomic_store(i64 noundef 16, ptr noundef %[[P]], ptr noundef %[[TMP]], i32 noundef 5)

// Ensure runtime atomic declarations carry nounwind+willreturn parity attrs.
// __atomic_load/__atomic_store get noundef via callee-centric ABI lowering.
// __atomic_compare_exchange from LowerToLLVM path has bare declaration but
// call sites carry zeroext+noundef (matching OGCG call-site attributes).
// LLVMATTR-DAG: declare void @__atomic_load(i64 noundef, ptr noundef, ptr noundef, i32 noundef) #[[ATOMIC_ATTRS:[0-9]+]]
// LLVMATTR-DAG: declare void @__atomic_store(i64 noundef, ptr noundef, ptr noundef, i32 noundef) #[[ATOMIC_ATTRS]]
// LLVMATTR-DAG: declare i1 @__atomic_compare_exchange(i64, ptr, ptr, ptr, i32, i32) #[[ATOMIC_ATTRS]]
// LLVMATTR-DAG: attributes #[[ATOMIC_ATTRS]] = { nounwind willreturn }
