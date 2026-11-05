// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fclangir -emit-cir -std=c++11 -O1 -disable-llvm-passes -o - %s \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fclangir -emit-llvm -std=c++11 -O1 -disable-llvm-passes -o - %s \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -emit-llvm -std=c++11 -O1 -disable-llvm-passes -o - %s \
// RUN:   | FileCheck %s --check-prefix=OGCG

struct Base0 {
  void nonvirtual0();
  virtual void virtual1();
};

typedef void (Base0::*MethodTy0)();

// CIR: cir.ptrauth_cxx_vtable_pointer_auth_mode = 3 : i32
// CIR: cir.ptrauth_cxx_vtable_pointer_key = 2 : i32

// LLVM-LABEL: define{{.*}} void @_Z5test1P5Base0MS_FvvE
// LLVM: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}
// LLVM: %[[VTABLE:.*]] = load ptr, ptr %{{.*}}, align 8
// LLVM: %[[VTABLE_INT:.*]] = ptrtoint ptr %[[VTABLE]] to i64
// LLVM: %[[AUTHED_INT:.*]] = call i64 @llvm.ptrauth.auth(i64 %[[VTABLE_INT]], i32 2, i64 0)
// LLVM: %[[AUTHED_VTABLE:.*]] = inttoptr i64 %[[AUTHED_INT]] to ptr
// LLVM: getelementptr i8, ptr %[[AUTHED_VTABLE]], i64 %{{.*}}
// LLVM: call void %{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) [ "ptrauth"(i32 0, i64 %{{.*}}) ]

// OGCG-LABEL: define{{.*}} void @_Z5test1P5Base0MS_FvvE
// OGCG: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}
// OGCG: %[[VTABLE:.*]] = load ptr, ptr %{{.*}}, align 8
// OGCG: %[[VTABLE_INT:.*]] = ptrtoint ptr %[[VTABLE]] to i64
// OGCG: %[[AUTHED_INT:.*]] = call i64 @llvm.ptrauth.auth(i64 %[[VTABLE_INT]], i32 2, i64 0)
// OGCG: %[[AUTHED_VTABLE:.*]] = inttoptr i64 %[[AUTHED_INT]] to ptr
// OGCG: getelementptr i8, ptr %[[AUTHED_VTABLE]], i64 %{{.*}}

void test1(Base0 *a0, MethodTy0 a1) {
  (a0->*a1)();
}
