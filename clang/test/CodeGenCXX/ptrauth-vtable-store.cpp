// RUN: %clang_cc1 %s -x c++ -std=c++23 -triple arm64-apple-ios -fptrauth-calls -fptrauth-vtable-pointer-type-discrimination -emit-llvm -O1 -disable-llvm-passes -no-enable-noundef-analysis -o - | FileCheck --check-prefix=TYPEAUTH %s
// RUN: %if cir-support %{ %clang_cc1 %s -x c++ -std=c++23 -triple arm64-apple-ios -fptrauth-calls -fptrauth-vtable-pointer-type-discrimination -fclangir -emit-llvm -O1 -disable-llvm-passes -no-enable-noundef-analysis -o - | FileCheck --check-prefix=TYPEAUTH %s %}
// RUN: %clang_cc1 %s -x c++ -std=c++23 -triple arm64-apple-ios -fptrauth-calls -fptrauth-vtable-pointer-address-discrimination -emit-llvm -O1 -disable-llvm-passes -no-enable-noundef-analysis -o - | FileCheck --check-prefix=ADDRESSAUTH %s
// RUN: %if cir-support %{ %clang_cc1 %s -x c++ -std=c++23 -triple arm64-apple-ios -fptrauth-calls -fptrauth-vtable-pointer-address-discrimination -fclangir -emit-llvm -O1 -disable-llvm-passes -no-enable-noundef-analysis -o - | FileCheck --check-prefix=ADDRESSAUTH %s %}
// RUN: %clang_cc1 %s -x c++ -std=c++23 -triple arm64-apple-ios -fptrauth-calls -fptrauth-vtable-pointer-type-discrimination -fptrauth-vtable-pointer-address-discrimination -emit-llvm -O1 -disable-llvm-passes -no-enable-noundef-analysis -o - | FileCheck --check-prefix=BOTHAUTH %s
// RUN: %if cir-support %{ %clang_cc1 %s -x c++ -std=c++23 -triple arm64-apple-ios -fptrauth-calls -fptrauth-vtable-pointer-type-discrimination -fptrauth-vtable-pointer-address-discrimination -fclangir -emit-llvm -O1 -disable-llvm-passes -no-enable-noundef-analysis -o - | FileCheck --check-prefix=BOTHAUTH %s %}

struct A {
  A();
  virtual void fa();
};

struct B {
  B();
  virtual void fb();
};

struct C : A, B {
  C();
};

A::A() = default;
B::B() = default;
C::C() = default;

void A::fa() {}
void B::fb() {}

// TYPEAUTH-LABEL: define {{.*}}ptr @_ZN1CC2Ev(
// TYPEAUTH:         [[THIS1:%.*]] = load ptr, ptr [[THIS_ADDR:%.*]], align 8
// TYPEAUTH:         [[CALL:%.*]] = call ptr @_ZN1AC2Ev(ptr nonnull align 8 dereferenceable(8) [[THIS1]])
// TYPEAUTH:         [[B_BASE:%.*]] = getelementptr inbounds i8, ptr [[THIS1]], i64 8
// TYPEAUTH:         [[CALL2:%.*]] = call ptr @_ZN1BC2Ev(ptr nonnull align 8 dereferenceable(8) [[B_BASE]])
// TYPEAUTH:         [[SIGNED0_INT:%.*]] = call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr getelementptr inbounds inrange(-16, 8) ({ [3 x ptr], [3 x ptr] }, ptr @_ZTV1C, i32 0, i32 0, i32 2) to i64), i32 2, i64 [[DISC0:[0-9]+]])
// TYPEAUTH:         [[SIGNED0:%.*]] = inttoptr i64 [[SIGNED0_INT]] to ptr
// TYPEAUTH:         store ptr [[SIGNED0]], ptr [[THIS1]], align 8, !tbaa [[TBAA_VPTR:![0-9]+]]
// TYPEAUTH:         [[B_STORE:%.*]] = getelementptr inbounds i8, ptr [[THIS1]], i64 8
// TYPEAUTH:         [[SIGNED1_INT:%.*]] = call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr getelementptr inbounds inrange(-16, 8) ({ [3 x ptr], [3 x ptr] }, ptr @_ZTV1C, i32 0, i32 1, i32 2) to i64), i32 2, i64 [[DISC1:[0-9]+]])
// TYPEAUTH:         [[SIGNED1:%.*]] = inttoptr i64 [[SIGNED1_INT]] to ptr
// TYPEAUTH:         store ptr [[SIGNED1]], ptr [[B_STORE]], align 8, !tbaa [[TBAA_VPTR]]

// ADDRESSAUTH-LABEL: define {{.*}}ptr @_ZN1CC2Ev(
// ADDRESSAUTH:         [[THIS1:%.*]] = load ptr, ptr [[THIS_ADDR:%.*]], align 8
// ADDRESSAUTH:         [[CALL:%.*]] = call ptr @_ZN1AC2Ev(ptr nonnull align 8 dereferenceable(8) [[THIS1]])
// ADDRESSAUTH:         [[B_BASE:%.*]] = getelementptr inbounds i8, ptr [[THIS1]], i64 8
// ADDRESSAUTH:         [[CALL2:%.*]] = call ptr @_ZN1BC2Ev(ptr nonnull align 8 dereferenceable(8) [[B_BASE]])
// ADDRESSAUTH:         [[THIS_INT:%.*]] = ptrtoint ptr [[THIS1]] to i64
// ADDRESSAUTH:         [[SIGNED0_INT:%.*]] = call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr getelementptr inbounds inrange(-16, 8) ({ [3 x ptr], [3 x ptr] }, ptr @_ZTV1C, i32 0, i32 0, i32 2) to i64), i32 2, i64 [[THIS_INT]])
// ADDRESSAUTH:         [[SIGNED0:%.*]] = inttoptr i64 [[SIGNED0_INT]] to ptr
// ADDRESSAUTH:         store ptr [[SIGNED0]], ptr [[THIS1]], align 8, !tbaa [[TBAA_VPTR:![0-9]+]]
// ADDRESSAUTH:         [[B_STORE:%.*]] = getelementptr inbounds i8, ptr [[THIS1]], i64 8
// ADDRESSAUTH:         [[B_BASE_INT:%.*]] = ptrtoint ptr [[B_STORE]] to i64
// ADDRESSAUTH:         [[SIGNED1_INT:%.*]] = call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr getelementptr inbounds inrange(-16, 8) ({ [3 x ptr], [3 x ptr] }, ptr @_ZTV1C, i32 0, i32 1, i32 2) to i64), i32 2, i64 [[B_BASE_INT]])
// ADDRESSAUTH:         [[SIGNED1:%.*]] = inttoptr i64 [[SIGNED1_INT]] to ptr
// ADDRESSAUTH:         store ptr [[SIGNED1]], ptr [[B_STORE]], align 8, !tbaa [[TBAA_VPTR]]

// BOTHAUTH-LABEL: define {{.*}}ptr @_ZN1CC2Ev(
// BOTHAUTH:         [[THIS1:%.*]] = load ptr, ptr [[THIS_ADDR:%.*]], align 8
// BOTHAUTH:         [[CALL:%.*]] = call ptr @_ZN1AC2Ev(ptr nonnull align 8 dereferenceable(8) [[THIS1]])
// BOTHAUTH:         [[B_BASE:%.*]] = getelementptr inbounds i8, ptr [[THIS1]], i64 8
// BOTHAUTH:         [[CALL2:%.*]] = call ptr @_ZN1BC2Ev(ptr nonnull align 8 dereferenceable(8) [[B_BASE]])
// BOTHAUTH:         [[THIS_INT:%.*]] = ptrtoint ptr [[THIS1]] to i64
// BOTHAUTH:         [[BLEND0:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[THIS_INT]], i64 [[DISC0:[0-9]+]])
// BOTHAUTH:         [[SIGNED0_INT:%.*]] = call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr getelementptr inbounds inrange(-16, 8) ({ [3 x ptr], [3 x ptr] }, ptr @_ZTV1C, i32 0, i32 0, i32 2) to i64), i32 2, i64 [[BLEND0]])
// BOTHAUTH:         [[SIGNED0:%.*]] = inttoptr i64 [[SIGNED0_INT]] to ptr
// BOTHAUTH:         store ptr [[SIGNED0]], ptr [[THIS1]], align 8, !tbaa [[TBAA_VPTR:![0-9]+]]
// BOTHAUTH:         [[B_STORE:%.*]] = getelementptr inbounds i8, ptr [[THIS1]], i64 8
// BOTHAUTH:         [[B_BASE_INT:%.*]] = ptrtoint ptr [[B_STORE]] to i64
// BOTHAUTH:         [[BLEND1:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[B_BASE_INT]], i64 [[DISC1:[0-9]+]])
// BOTHAUTH:         [[SIGNED1_INT:%.*]] = call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr getelementptr inbounds inrange(-16, 8) ({ [3 x ptr], [3 x ptr] }, ptr @_ZTV1C, i32 0, i32 1, i32 2) to i64), i32 2, i64 [[BLEND1]])
// BOTHAUTH:         [[SIGNED1:%.*]] = inttoptr i64 [[SIGNED1_INT]] to ptr
// BOTHAUTH:         store ptr [[SIGNED1]], ptr [[B_STORE]], align 8, !tbaa [[TBAA_VPTR]]

void test() {
  C c;
}
