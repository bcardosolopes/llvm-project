// RUN: %clang_cc1 -triple arm64-apple-ios -fclangir -emit-mlir=cir-abi %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple arm64-apple-ios -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM --input-file=%t.ll
// RUN: %clang_cc1 -triple arm64-apple-ios -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --check-prefix=OGCG --input-file=%t.ogcg.ll

// On Darwin C++ ABIs, complete destructors return `this` rather than `void`.
// Ensure CIR still emits a valid direct global-dtor registration call in the
// dtor region / lowered init function instead of creating a void-typed cir.call
// to a non-void destructor.

struct S {
  ~S() {}
};

S global;

// CIR: cir.func {{.*}} @_ZN1SD1Ev({{.*}}) -> (!cir.ptr<!rec_S>
// CIR: cir.func {{.*}} @__cxx_global_var_init()
// CIR: %[[DTOR:.*]] = cir.get_global @_ZN1SD1Ev : !cir.ptr<!cir.func<(!cir.ptr<!rec_S>) -> !cir.ptr<!rec_S>>>
// CIR: %[[CAST:.*]] = cir.cast bitcast %[[DTOR]] : !cir.ptr<!cir.func<(!cir.ptr<!rec_S>) -> !cir.ptr<!rec_S>>> -> !cir.ptr<!cir.func<(!cir.ptr<!void>)>>
// CIR: %[[ATEXIT:.*]] = cir.call @__cxa_atexit(%[[CAST]], {{.*}}) nothrow : (!cir.ptr<!cir.func<(!cir.ptr<!void>)>>, !cir.ptr<!void>, !cir.ptr<i8>) -> !s32i

// LLVM-DAG: define internal void @__cxx_global_var_init()
// LLVM-DAG: call i32 @__cxa_atexit(ptr @_ZN1SD1Ev, ptr @global, ptr @__dso_handle)
// LLVM-DAG: define linkonce_odr noundef ptr @_ZN1SD1Ev(ptr noundef nonnull align 1 dereferenceable(1)

// OGCG-DAG: define internal void @__cxx_global_var_init()
// OGCG-DAG: call i32 @__cxa_atexit(ptr @_ZN1SD1Ev, ptr @global, ptr @__dso_handle)
// OGCG-DAG: define linkonce_odr noundef ptr @_ZN1SD1Ev(ptr noundef nonnull align 1 dereferenceable(1)
