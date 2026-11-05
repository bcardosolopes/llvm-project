// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that reference temporaries with array types and destructors
// properly emit __cxa_atexit registration for array destruction.

struct S {
  int x;
  ~S();
};

const S (&arr_ref)[2] = {S{1}, S{2}};

// CIR-LABEL: cir.func no_inline internal private @__cxx_global_array_dtor
// CIR:   cir.call @_ZN1SD1Ev
// CIR: cir.global {{.*}} @arr_ref
// CIR-LABEL: cir.func no_inline internal private @__cxx_global_var_init() section(".text.startup"){{.*}}{
// CIR:   %[[HANDLE:.*]] = cir.get_global @__dso_handle : !cir.ptr<{{.*}}>
// CIR:   %[[DTOR:.*]] = cir.get_global @__cxx_global_array_dtor : !cir.ptr<!cir.func<(!cir.ptr<!void>)>>
// CIR:   %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!void>
// CIR:   %{{.*}} = cir.call @__cxa_atexit(%[[DTOR]], %[[NULL]], %[[HANDLE]]) nothrow : (!cir.ptr<!cir.func<(!cir.ptr<!void>)>>, !cir.ptr<!void>, !cir.ptr<{{.*}}>) -> !s32i
// CIR: cir.global {{.*}} @_ZGR7arr_ref_ {{.*}} !cir.array<!rec_S x 2>
// CIR-LABEL: cir.func no_inline internal private @_GLOBAL__sub_I_global_array_destruction.cpp
// CIR: cir.call @__cxx_global_var_init() : () -> ()
// CIR-NOT: @__cxx_global_var_init.1

// LLVM: define internal void @__cxx_global_array_dtor(ptr noundef %0) #{{[0-9]+}}
// LLVM:   call void @_ZN1SD1Ev
// LLVM:   ret void

// LLVM: call {{.*}} @__cxa_atexit(ptr @__cxx_global_array_dtor

// OGCG: call {{.*}} @__cxa_atexit(ptr @__cxx_global_array_dtor
// OGCG: define internal void @__cxx_global_array_dtor(ptr noundef %0)
// OGCG:   call void @_ZN1SD1Ev
// OGCG:   ret void
