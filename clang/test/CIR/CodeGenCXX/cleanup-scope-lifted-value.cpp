// Truncating the guarded range at the first op whose result has no users yet is
// not enough on its own: a value can have a user inside the range and *still* be
// handed upwards to the enclosing full expression, so an in-range use proves
// nothing about the value being finished. Address computations that take nothing
// the range defines -- pure ops, allocas, non-virtual base adjustments -- are
// therefore lifted above the range unconditionally, which is always valid and
// costs nothing, since none of them throw and so none needs a pad.
//
// RUN: %clang_cc1 -std=c++17 -triple aarch64-linux-android23 -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

struct Ptr { Ptr(); Ptr(const Ptr &); ~Ptr(); };
Ptr make();
struct Holder { Holder(Ptr); ~Holder(); };

// The new-expression's pointer is bitcast to Holder*, passed to the constructor
// inside the guarded range, and then yielded out of the enclosing cir.scope to
// initialize the static. Sweeping the bitcast into the region would leave that
// yield referring to a value defined inside it.
const Holder &instance() {
  static auto *k = new Holder(make());
  return *k;
}

// CIR-LABEL: cir.func{{.*}} @_Z8instancev
// CIR:   %[[RES:.*]] = cir.scope {
// CIR:     %[[SLOT:.*]] = cir.alloca !cir.ptr<!void>, {{.*}} ["tmp.try.call.res"]
// CIR:     %[[TMP:.*]] = cir.alloca !rec_Ptr, {{.*}} ["agg.tmp0"]
// CIR:     %[[MEM:.*]] = cir.call exception @_Znwm
// CIR:     cir.store{{.*}} %[[MEM]], %[[SLOT]]
// CIR:     %[[LD:.*]] = cir.load{{.*}} %[[SLOT]]
//
// The bitcast is lifted out of the range, above the scope that guards the
// operator-delete cleanup, so it dominates both its in-range use and the yield.
// CIR:     %[[OBJ:.*]] = cir.cast bitcast %[[LD]] : !cir.ptr<!void> -> !cir.ptr<!rec_Holder>
// CIR:     cir.cleanup.scope {
// CIR:       cir.call exception @_Z4makev(%[[TMP]])
// CIR:       cir.cleanup.scope {
// CIR:         cir.call exception @_ZN6HolderC1E3Ptr(%[[OBJ]], %[[TMP]])
// CIR:         cir.yield
// CIR:       } cleanup {
// CIR:         cir.call @_ZN3PtrD1Ev(%[[TMP]])
// CIR:         cir.yield
// CIR:       }
// CIR:       cir.call @_ZN3PtrD1Ev(%[[TMP]])
// CIR:       cir.yield
// CIR:     } cleanup {
// The scope's own cleanup frees the memory if the construction throws.
// CIR:       cir.call @_ZdlPvm(%[[LD]]
// CIR:       cir.yield
// CIR:     }
// CIR:     cir.yield %[[OBJ]] : !cir.ptr<!rec_Holder>
// CIR:   } : !cir.ptr<!rec_Holder>
