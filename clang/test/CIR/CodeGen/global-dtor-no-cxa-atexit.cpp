// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fno-use-cxa-atexit -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fno-use-cxa-atexit -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fno-use-cxa-atexit -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// When -fno-use-cxa-atexit is passed, global destructors should
// use atexit() with a __dtor_<varname> wrapper instead of __cxa_atexit().

struct A { A(); ~A(); };
A a;

// CIR: #{{.*}} = #cir<extra({nothrow})>
// CIR-LABEL: cir.func no_inline internal private @__cxx_global_var_init()
// CIR: cir.call @_ZN1AC1Ev(
// CIR: %{{.*}} = cir.call @atexit(%{{.*}}) nothrow : (!cir.ptr<!cir.func<()>>) -> !s32i

// LLVM-LABEL: define internal void @__cxx_global_var_init()
// LLVM:   call void @_ZN1AC1Ev(
// LLVM:   %{{.*}} = call i32 @atexit(ptr @__dtor_a) #[[ATEXIT_ATTR:[0-9]+]]

// LLVM-LABEL: define internal void @__dtor_a()
// LLVM:   call void @_ZN1AD1Ev(ptr
// LLVM:   @a

// LLVM: declare i32 @atexit(ptr) #[[ATEXIT_ATTR]]

// LLVM-NOT: __cxa_atexit
// LLVM-NOT: __dso_handle

// OGCG: define internal void @__cxx_global_var_init()
// OGCG:   call void @_ZN1AC1Ev(
// OGCG:   call {{.*}}i32 @atexit(ptr {{.*}}@__dtor_a)

// OGCG: define internal void @__dtor_a()
// OGCG:   call void @_ZN1AD1Ev(ptr
// OGCG:   @a

// OGCG: declare {{.*}}i32 @atexit(ptr

// OGCG-NOT: __cxa_atexit
// OGCG-NOT: __dso_handle
