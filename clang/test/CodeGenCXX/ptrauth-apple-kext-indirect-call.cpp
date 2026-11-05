// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fapple-kext -emit-llvm -o - %s | FileCheck %s --check-prefix=CHECK
// RUN: %if cir-support %{ %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fapple-kext -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR %}

// CHECK: @_ZTV5TemplIiE = internal unnamed_addr constant { [5 x ptr] } { [5 x ptr] [ptr null, ptr @_ZTI5TemplIiE, ptr ptrauth (ptr @_ZN5TemplIiE1fEv, i32 0, i64 22189, ptr getelementptr inbounds ({ [5 x ptr] }, ptr @_ZTV5TemplIiE, i32 0, i32 0, i32 2)), ptr ptrauth (ptr @_ZN5TemplIiE1gEv, i32 0, i64 9912, ptr getelementptr inbounds ({ [5 x ptr] }, ptr @_ZTV5TemplIiE, i32 0, i32 0, i32 3)), ptr null] }, align 8

struct Base {
  virtual void abc(void) const;
};

void Base::abc(void) const {}

void FUNC(Base* p) {
  p->Base::abc();
}

// CHECK: getelementptr inbounds (ptr, ptr @_ZTV4Base, i64 2)
// CHECK-NOT: call void @_ZNK4Base3abcEv
// CIR-LABEL: define void @_Z4FUNCP4Base
// CIR: [[VF1:%.*]] = load ptr, ptr getelementptr inbounds nuw (i8, ptr @_ZTV4Base, i64 16), align 8
// CIR-NEXT: [[BLEND1:%.*]] = call i64 @llvm.ptrauth.blend(i64 ptrtoint (ptr getelementptr inbounds nuw (i8, ptr @_ZTV4Base, i64 16) to i64), i64 64320)
// CIR-NEXT: call void [[VF1]](ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) [ "ptrauth"(i32 0, i64 [[BLEND1]]) ]

template<class T>
struct Templ {
  virtual void f() {}
  virtual void g() {}
};
template<class T>
struct SubTempl : public Templ<T> {
  virtual void f() {} // override
  virtual void g() {} // override
};

void f(SubTempl<int>* t) {
  // Qualified calls go through the (qualified) vtable in apple-kext mode.
  // Since t's this pointer points to SubTempl's vtable, the call needs
  // to load Templ<int>'s vtable.  Hence, Templ<int>::g needs to be
  // instantiated in this TU, for it's referenced by the vtable.
  // (This happens only in apple-kext mode; elsewhere virtual calls can always
  // use the vtable pointer off this instead of having to load the vtable
  // symbol.)
  t->Templ::f();
}

// CHECK: getelementptr inbounds (ptr, ptr @_ZTV5TemplIiE, i64 2)
// CHECK: define internal void @_ZN5TemplIiE1fEv(ptr noundef nonnull align {{[0-9]+}} dereferenceable(8) %this)
// CHECK: define internal void @_ZN5TemplIiE1gEv(ptr noundef nonnull align {{[0-9]+}} dereferenceable(8) %this)
// CIR-LABEL: define void @_Z1fP8SubTemplIiE
// CIR: [[VF2:%.*]] = load ptr, ptr getelementptr inbounds nuw (i8, ptr @_ZTV5TemplIiE, i64 16), align 8
// CIR-NEXT: [[BLEND2:%.*]] = call i64 @llvm.ptrauth.blend(i64 ptrtoint (ptr getelementptr inbounds nuw (i8, ptr @_ZTV5TemplIiE, i64 16) to i64), i64 22189)
// CIR-NEXT: call void [[VF2]](ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}) [ "ptrauth"(i32 0, i64 [[BLEND2]]) ]
