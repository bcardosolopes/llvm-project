// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// TLS wrapper for extern thread_local variables must null-check
// and call the extern_weak init function before accessing the TLS address.

extern thread_local int b;
int *get_b() { return &b; }

// CIR: cir.func{{.*}} @_ZTW1b() -> (!cir.ptr<!s32i>
// CIR: %[[INITFN_ADDR:.*]] = cir.get_global @_ZTH1b : !cir.ptr<!cir.func<()>>
// CIR: %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!cir.func<()>>
// CIR: %[[NON_NULL:.*]] = cir.cmp(ne, %[[INITFN_ADDR]], %[[NULL]]) : !cir.ptr<!cir.func<()>>, !cir.bool
// CIR: cir.if %[[NON_NULL]] {
// CIR:   cir.call @_ZTH1b() : () -> ()
// CIR: %[[TLS_ADDR:.*]] = cir.get_global thread_local @b : !cir.ptr<!s32i>
// CIR: cir.return %[[TLS_ADDR]] : !cir.ptr<!s32i>
// CIR: cir.func extern_weak private @_ZTH1b()

// The wrapper should null-check @_ZTH1b (extern_weak) and call it if non-null.
// CHECK: define linkonce_odr hidden noundef ptr @_ZTW1b()
// CHECK: icmp ne ptr @_ZTH1b, null
// CHECK: call void @_ZTH1b()
// CHECK: call {{.*}} ptr @llvm.threadlocal.address
// CHECK: declare extern_weak void @_ZTH1b()

// OGCG: define linkonce_odr hidden noundef ptr @_ZTW1b()
// OGCG: icmp ne ptr @_ZTH1b, null
// OGCG: call void @_ZTH1b()
// OGCG: call {{.*}} ptr @llvm.threadlocal.address
// OGCG: declare extern_weak void @_ZTH1b()
