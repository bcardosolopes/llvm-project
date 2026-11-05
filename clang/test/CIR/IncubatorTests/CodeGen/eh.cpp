// RUN: %clang_cc1 -std=c++20 -triple aarch64-none-linux-android21 -fclangir -fcxx-exceptions -fexceptions -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple aarch64-none-linux-android21 -fclangir -fcxx-exceptions -fexceptions -emit-llvm -fno-clangir-call-conv-lowering %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

struct test1_D {
  double d;
} d1;

void test1() {
  throw d1;
}

// CIR-LABEL: @_Z5test1v
// CIR:   %[[ALLOC:.*]] = cir.alloc.exception 8 -> !cir.ptr<!rec_test1_D>
// CIR:   %[[G:.*]] = cir.get_global @d1 : !cir.ptr<!rec_test1_D>
// CIR:   cir.copy %[[G]] to %[[ALLOC]] {{.*}}: !cir.ptr<!rec_test1_D>
// CIR:   cir.throw %[[ALLOC]] : !cir.ptr<!rec_test1_D>, @_ZTI7test1_D
// CIR:   cir.unreachable
// CIR: }

// LLVM-LABEL: @_Z5test1v
// LLVM:   %[[ALLOC:.*]] = call ptr @__cxa_allocate_exception(i64 8)
// LLVM:   call void @llvm.memcpy.p0.p0.i64(ptr{{.*}} %{{.*}}, ptr{{.*}} @d1, i64 8, i1 false)
// LLVM:   call void @__cxa_throw(ptr %[[ALLOC]], ptr @_ZTI7test1_D, ptr null)
// LLVM:   unreachable
// LLVM: }

struct test2_D {
  test2_D(const test2_D&o);
  test2_D();
  virtual void bar() { }
  int i; int j;
} d2;

void test2() {
  throw d2;
}

// The copy ctor may throw, so the exception storage is guarded by a
// cir.cleanup.scope that frees it on the unwind edge.
// CIR-LABEL: @_Z5test2v
// CIR:   %[[ALLOC:.*]] = cir.alloc.exception 16 -> !cir.ptr<!rec_test2_D>
// Address computations are lifted out of the guarded range: they cannot throw,
// so keeping them inside would buy no sharing, and a value the enclosing
// expression still needs must not be trapped in the region.
// CIR:   %[[G:.*]] = cir.get_global @d2 : !cir.ptr<!rec_test2_D>
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZN7test2_DC1ERKS_(%[[ALLOC]], %[[G]])
// CIR:     cir.yield
// CIR:   } cleanup {
// CIR:     cir.call @__cxa_free_exception
// CIR:     cir.yield
// CIR:   }
// CIR:   cir.throw %[[ALLOC]] : !cir.ptr<!rec_test2_D>, @_ZTI7test2_D

// LLVM-LABEL: @_Z5test2v
// LLVM: %[[ALLOC:.*]] = call ptr @__cxa_allocate_exception(i64 16)
// LLVM: invoke void @_ZN7test2_DC1ERKS_(ptr {{[^,]*}}%[[ALLOC]], ptr {{.*}}@d2)
// LLVM: landingpad { ptr, i32 }
// LLVM:         cleanup
// LLVM: call void @__cxa_free_exception(ptr %[[ALLOC]])
// LLVM: call void @__cxa_throw(ptr %[[ALLOC]], ptr @_ZTI7test2_D, ptr null)
