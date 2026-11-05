// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s
// RUN: FileCheck --check-prefix=NOC1 --input-file=%t.cir %s
// RUN: FileCheck --check-prefix=NOC1 --input-file=%t-cir.ll %s
// RUN: FileCheck --check-prefix=NOC1 --input-file=%t.ll %s

struct B {
  B();
};
B::B() {
}

// LLVM: @_ZN1BC1Ev = unnamed_addr alias void (ptr), ptr @_ZN1BC2Ev

// OGCG: @_ZN1BC1Ev = unnamed_addr alias void (ptr), ptr @_ZN1BC2Ev

// CHECK: cir.func{{.*}} @_ZN1BC2Ev(%arg0: !cir.ptr<!rec_B>
// CHECK:   %[[THIS_ADDR:.*]] = cir.alloca !cir.ptr<!rec_B>, !cir.ptr<!cir.ptr<!rec_B>>, ["this", init]
// CHECK:   cir.store %arg0, %[[THIS_ADDR]]
// CHECK:   %[[THIS:.*]] = cir.load %[[THIS_ADDR]] : !cir.ptr<!cir.ptr<!rec_B>>, !cir.ptr<!rec_B>

// CHECK: cir.func{{.*}} private @_ZN1BC1Ev(!cir.ptr<!rec_B> {{.*}}) alias(@_ZN1BC2Ev)

// LLVM: define{{.*}} @_ZN1BC2Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[THIS_ARG:.*]])
// LLVM:   %[[THIS_ADDR:.*]] = alloca ptr
// LLVM:   store ptr %[[THIS_ARG]], ptr %[[THIS_ADDR]]
// LLVM:   %[[THIS:.*]] = load ptr, ptr %[[THIS_ADDR]]

// OGCG: define{{.*}} @_ZN1BC2Ev(ptr{{.*}} %[[THIS_ARG:.*]])
// OGCG:   %[[THIS_ADDR:.*]] = alloca ptr
// OGCG:   store ptr %[[THIS_ARG]], ptr %[[THIS_ADDR]]
// OGCG:   %[[THIS:.*]] = load ptr, ptr %[[THIS_ADDR]]

void bar() {
  B b;
}

// CHECK: cir.func{{.*}} @_Z3barv()
// CHECK:   %[[B:.*]] = cir.alloca !rec_B, !cir.ptr<!rec_B>, ["b", init]
// CHECK:   cir.call @_ZN1BC1Ev(%[[B]]) : (!cir.ptr<!rec_B>) -> ()
// CHECK:   cir.return

// LLVM: define{{.*}} void @_Z3barv()
// LLVM:   %[[B:.*]] = alloca %struct.B, i64 1, align 1
// LLVM:   call void @_ZN1BC1Ev(ptr{{.*}} %[[B]])
// LLVM:   ret void

// OGCG: define{{.*}} void @_Z3barv()
// OGCG:   %[[B:.*]] = alloca %struct.B, align 1
// OGCG:   call void @_ZN1BC1Ev(ptr{{.*}} %[[B]])
// OGCG:   ret void

// Struk's constructor is linkonce_odr, so it cannot be given an alias. OGCG
// drops the complete-object constructor and points every reference at the
// base one instead. CIR does the same, except that the fold happens in
// LoweringPrepare rather than during CIRGen: CIRGen emits C1 as a delegating
// stub tagged with #cir.structor_dedup, and LoweringPrepare rewrites the
// references and erases it once the module is complete.
struct Struk {
  Struk() {}
};

void baz() {
  Struk s;
}

// NOC1-NOT: _ZN5StrukC1Ev

// CHECK:   cir.func{{.*}} @_Z3bazv()
// CHECK:     %[[S_ADDR:.*]] = cir.alloca !rec_Struk, !cir.ptr<!rec_Struk>, ["s", init]
// CHECK:     cir.call @_ZN5StrukC2Ev(%[[S_ADDR]]) : (!cir.ptr<!rec_Struk>) -> ()

// CHECK:   cir.func{{.*}} linkonce_odr @_ZN5StrukC2Ev(%arg0: !cir.ptr<!rec_Struk>
// CHECK:     %[[THIS_ADDR:.*]] = cir.alloca !cir.ptr<!rec_Struk>, !cir.ptr<!cir.ptr<!rec_Struk>>, ["this", init]
// CHECK:     cir.store %arg0, %[[THIS_ADDR]] : !cir.ptr<!rec_Struk>, !cir.ptr<!cir.ptr<!rec_Struk>>
// CHECK:     %[[THIS:.*]] = cir.load %[[THIS_ADDR]] : !cir.ptr<!cir.ptr<!rec_Struk>>, !cir.ptr<!rec_Struk>
// CHECK:     cir.return

// The tag is internal to the pipeline and must never reach the output.
// CHECK-NOT: cir.structor_dedup

// LLVM: define{{.*}} void @_Z3bazv()
// LLVM:   %[[S_ADDR:.*]] = alloca %struct.Struk
// LLVM:   call void @_ZN5StrukC2Ev(ptr{{.*}} %[[S_ADDR]])

// LLVM: define linkonce_odr void @_ZN5StrukC2Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[THIS_ARG2:.*]])
// LLVM:   %[[THIS_ADDR:.*]] = alloca ptr
// LLVM:   store ptr %[[THIS_ARG2]], ptr %[[THIS_ADDR]]
// LLVM:   %[[THIS:.*]] = load ptr, ptr %[[THIS_ADDR]]

// OGCG: define{{.*}} void @_Z3bazv()
// OGCG:   %[[S_ADDR:.*]] = alloca %struct.Struk
// OGCG:   call void @_ZN5StrukC2Ev(ptr{{.*}} %[[S_ADDR]])

// OGCG: define linkonce_odr void @_ZN5StrukC2Ev(ptr{{.*}} %[[THIS_ARG]])
// OGCG:   %[[THIS_ADDR:.*]] = alloca ptr
// OGCG:   store ptr %[[THIS_ARG]], ptr %[[THIS_ADDR]]
// OGCG:   %[[THIS:.*]] = load ptr, ptr %[[THIS_ADDR]]
