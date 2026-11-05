// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fregister-global-dtors-with-atexit %s -o - | FileCheck %s --check-prefix=CIR-CXA
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fregister-global-dtors-with-atexit -fno-use-cxa-atexit %s -o - | FileCheck %s --check-prefix=CIR-ATEXIT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fregister-global-dtors-with-atexit %s -o - | FileCheck %s --check-prefix=LLVM-CXA
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fregister-global-dtors-with-atexit -fno-use-cxa-atexit %s -o - | FileCheck %s --check-prefix=LLVM-ATEXIT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fregister-global-dtors-with-atexit %s -o - | FileCheck %s --check-prefix=OGCG-CXA
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fregister-global-dtors-with-atexit -fno-use-cxa-atexit %s -o - | FileCheck %s --check-prefix=OGCG-ATEXIT

// For -fregister-global-dtors-with-atexit, CIR should mirror OGCG by
// emitting __GLOBAL_init_<priority> helpers in global_ctors and registering
// destructor-attribute functions through __cxa_atexit/atexit.

__attribute__((destructor(123))) static void d1(void) {}
__attribute__((destructor)) static void d2(void) {}

// CIR-CXA: #{{.*}} = #cir<extra({nothrow})>
// CIR-CXA-LABEL: cir.func no_inline internal private @__GLOBAL_init_123()
// CIR-CXA: %{{.*}} = cir.call @__cxa_atexit(%{{.*}}, %{{.*}}, %{{.*}}) nothrow : (!cir.ptr<!cir.func<(!cir.ptr<i8>)>>, !cir.ptr<i8>, !cir.ptr<i8>) -> !s32i
// CIR-CXA-LABEL: cir.func no_inline internal private @__GLOBAL_init_65535()
// CIR-CXA: %{{.*}} = cir.call @__cxa_atexit(%{{.*}}, %{{.*}}, %{{.*}}) nothrow : (!cir.ptr<!cir.func<(!cir.ptr<i8>)>>, !cir.ptr<i8>, !cir.ptr<i8>) -> !s32i

// CIR-ATEXIT: #{{.*}} = #cir<extra({nothrow})>
// CIR-ATEXIT-LABEL: cir.func no_inline internal private @__GLOBAL_init_123()
// CIR-ATEXIT: %{{.*}} = cir.call @atexit(%{{.*}}) nothrow : (!cir.ptr<!cir.func<()>>) -> !s32i
// CIR-ATEXIT-LABEL: cir.func no_inline internal private @__GLOBAL_init_65535()
// CIR-ATEXIT: %{{.*}} = cir.call @atexit(%{{.*}}) nothrow : (!cir.ptr<!cir.func<()>>) -> !s32i

// LLVM-CXA: @__dso_handle = external hidden global i8
// LLVM-CXA: @llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 123, ptr @__GLOBAL_init_123, ptr null }, { i32, ptr, ptr } { i32 65535, ptr @__GLOBAL_init_65535, ptr null }]
// LLVM-CXA-NOT: @llvm.global_dtors
// LLVM-CXA: declare i32 @__cxa_atexit(ptr, ptr, ptr) #[[CXA_ATTR:[0-9]+]]
// LLVM-CXA: define internal void @__GLOBAL_init_123(){{.*}}section ".text.startup"
// LLVM-CXA: call i32 @__cxa_atexit(ptr @d1, ptr null, ptr @__dso_handle) #[[CXA_ATTR]]
// LLVM-CXA: define internal void @__GLOBAL_init_65535(){{.*}}section ".text.startup"
// LLVM-CXA: call i32 @__cxa_atexit(ptr @d2, ptr null, ptr @__dso_handle) #[[CXA_ATTR]]

// LLVM-ATEXIT: @llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 123, ptr @__GLOBAL_init_123, ptr null }, { i32, ptr, ptr } { i32 65535, ptr @__GLOBAL_init_65535, ptr null }]
// LLVM-ATEXIT-NOT: @llvm.global_dtors
// LLVM-ATEXIT: declare i32 @atexit(ptr) #[[ATEXIT_ATTR:[0-9]+]]
// LLVM-ATEXIT: define internal void @__GLOBAL_init_123(){{.*}}section ".text.startup"
// LLVM-ATEXIT: call i32 @atexit(ptr @d1) #[[ATEXIT_ATTR]]
// LLVM-ATEXIT: define internal void @__GLOBAL_init_65535(){{.*}}section ".text.startup"
// LLVM-ATEXIT: call i32 @atexit(ptr @d2) #[[ATEXIT_ATTR]]
// LLVM-ATEXIT-NOT: @__dso_handle

// OGCG-CXA-DAG: @llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 123, ptr @__GLOBAL_init_123, ptr null }, { i32, ptr, ptr } { i32 65535, ptr @__GLOBAL_init_65535, ptr null }]
// OGCG-CXA-NOT: @llvm.global_dtors
// OGCG-CXA: define internal void @__GLOBAL_init_123(){{.*}}section ".text.startup"
// OGCG-CXA: call i32 @__cxa_atexit(ptr @d1, ptr null, ptr @__dso_handle)
// OGCG-CXA: define internal void @__GLOBAL_init_65535(){{.*}}section ".text.startup"
// OGCG-CXA: call i32 @__cxa_atexit(ptr @d2, ptr null, ptr @__dso_handle)

// OGCG-ATEXIT-DAG: @llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 123, ptr @__GLOBAL_init_123, ptr null }, { i32, ptr, ptr } { i32 65535, ptr @__GLOBAL_init_65535, ptr null }]
// OGCG-ATEXIT-NOT: @llvm.global_dtors
// OGCG-ATEXIT: define internal void @__GLOBAL_init_123(){{.*}}section ".text.startup"
// OGCG-ATEXIT: call i32 @atexit(ptr @d1)
// OGCG-ATEXIT: define internal void @__GLOBAL_init_65535(){{.*}}section ".text.startup"
// OGCG-ATEXIT: call i32 @atexit(ptr @d2)
