// RUN: %clang_cc1 -triple arm64-apple-ios15.0 -fobjc-runtime=ios-15.0 -x objective-c++ -std=c++17 -fobjc-arc -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple arm64-apple-ios15.0 -fobjc-runtime=ios-15.0 -x objective-c++ -std=c++17 -fobjc-arc -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// A C++ class with a __strong ObjC member gets compiler-synthesized special
// members. In the synthesized MOVE ctor/assignment the __strong member must be
// *moved* -- steal the pointer and null the source, with NO objc_retain -- not
// *copied* (retain, source left intact). The __weak member uses objc.moveWeak.
// The copy ctor and dtor keep their retain / copyWeak / storeStrong shape.
//
// The LLVM (ClangIR) and OGCG (classic) prefixes list their functions in each
// frontend's own emission order.

@class NSObject;

struct Holder {
  __strong id a;
  __weak id b;
  int n;
};

Holder moveThis(Holder o) { return Holder(static_cast<Holder &&>(o)); }
void moveassign(Holder &d, Holder &&s) { d = static_cast<Holder &&>(s); }
Holder copyThis(const Holder &o) { return Holder(o); }
void destroy(Holder *h) { h->~Holder(); }

// ---- ClangIR (LLVM prefix), in ClangIR emission order ----

// Move ctor: strong member stolen (load src.a, null src.a, store dst.a), NO
// retain; weak member uses moveWeak.
// LLVM-LABEL: define {{.*}}@_ZN6HolderC2EOS_(
// LLVM:         load ptr, ptr %{{.*}}, align 8, !nonnull
// LLVM:         %[[SA:.*]] = getelementptr {{.*}}%struct.Holder, ptr %{{.*}}, i32 0, i32 0
// LLVM-NEXT:    %[[V:.*]] = load ptr, ptr %[[SA]]
// LLVM-NEXT:    store ptr null, ptr %[[SA]]
// LLVM-NEXT:    store ptr %[[V]], ptr %{{.*}}
// LLVM:         call void @llvm.objc.moveWeak(
// LLVM-NOT:     @llvm.objc.retain(
// LLVM:       ret

// Dtor unchanged: destroyWeak + storeStrong(nil).
// LLVM-LABEL: define {{.*}}@_ZN6HolderD2Ev(
// LLVM:         call void @llvm.objc.destroyWeak(
// LLVM:         call void @llvm.objc.storeStrong(
// LLVM:       ret

// Move assignment: strong member stolen (null source) + old dest released, NO
// retain.
// LLVM-LABEL: define {{.*}}@_ZN6HolderaSEOS_(
// LLVM:         load ptr, ptr %{{.*}}, align 8, !nonnull
// LLVM:         %[[SA:.*]] = getelementptr {{.*}}%struct.Holder, ptr %{{.*}}, i32 0, i32 0
// LLVM-NEXT:    %[[V:.*]] = load ptr, ptr %[[SA]]
// LLVM-NEXT:    store ptr null, ptr %[[SA]]
// LLVM:         store ptr %[[V]], ptr %{{.*}}
// LLVM:         call void @llvm.objc.release(
// LLVM-NOT:     @llvm.objc.retain(
// LLVM:       ret

// Copy ctor unchanged: one retain for the strong member + copyWeak.
// LLVM-LABEL: define {{.*}}@_ZN6HolderC2ERKS_(
// LLVM:         call ptr @llvm.objc.retain(
// LLVM:         call void @llvm.objc.copyWeak(
// LLVM:       ret

// ---- Classic (OGCG prefix), in classic emission order ----

// OGCG-LABEL: define {{.*}}@_ZN6HolderaSEOS_(
// OGCG:         load ptr, ptr %{{.*}}, align 8, !nonnull
// OGCG:         %[[SA:.*]] = getelementptr {{.*}}%struct.Holder, ptr %{{.*}}, i32 0, i32 0
// OGCG-NEXT:    %[[V:.*]] = load ptr, ptr %[[SA]]
// OGCG-NEXT:    store ptr null, ptr %[[SA]]
// OGCG:         store ptr %[[V]], ptr %{{.*}}
// OGCG:         call void @llvm.objc.release(
// OGCG-NOT:     @llvm.objc.retain(
// OGCG:       ret

// OGCG-LABEL: define {{.*}}@_ZN6HolderC2EOS_(
// OGCG:         load ptr, ptr %{{.*}}, align 8, !nonnull
// OGCG:         %[[SA:.*]] = getelementptr {{.*}}%struct.Holder, ptr %{{.*}}, i32 0, i32 0
// OGCG-NEXT:    %[[V:.*]] = load ptr, ptr %[[SA]]
// OGCG-NEXT:    store ptr null, ptr %[[SA]]
// OGCG-NEXT:    store ptr %[[V]], ptr %{{.*}}
// OGCG:         call void @llvm.objc.moveWeak(
// OGCG-NOT:     @llvm.objc.retain(
// OGCG:       ret

// OGCG-LABEL: define {{.*}}@_ZN6HolderD2Ev(
// OGCG:         call void @llvm.objc.destroyWeak(
// OGCG:         call void @llvm.objc.storeStrong(
// OGCG:       ret

// OGCG-LABEL: define {{.*}}@_ZN6HolderC2ERKS_(
// OGCG:         call ptr @llvm.objc.retain(
// OGCG:         call void @llvm.objc.copyWeak(
// OGCG:       ret
