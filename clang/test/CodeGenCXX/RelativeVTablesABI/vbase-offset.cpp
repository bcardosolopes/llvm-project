// Check that the pointer adjustment from the virtual base offset is loaded as a
// 32-bit int.

// RUN: %clang_cc1 %s -triple=aarch64-unknown-fuchsia -o - -emit-llvm | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=aarch64-unknown-fuchsia -fclangir -emit-llvm -o - | FileCheck %s --check-prefix=CIR %}

// CHECK-LABEL: @_ZTv0_n12_N7Derived1fEi(
// CHECK-NEXT:  entry:
// CHECK:        [[vtable:%.+]] = load ptr, ptr %this1, align 8
// CHECK-NEXT:   [[vbase_offset_ptr:%.+]] = getelementptr inbounds i8, ptr [[vtable]], i64 -12
// CHECK-NEXT:   [[vbase_offset:%.+]] = load i32, ptr [[vbase_offset_ptr]], align 4
// CHECK-NEXT:   [[adj_this:%.+]] = getelementptr inbounds i8, ptr %this1, i32 [[vbase_offset]]
// CHECK:        [[call:%.+]] = tail call noundef i32 @_ZN7Derived1fEi(ptr noundef{{[^,]*}} [[adj_this]], i32 noundef {{.*}})
// CHECK:        ret i32 [[call]]
// CIR-LABEL: @_ZTv0_n12_N7Derived1fEi(
// CIR: [[THIS:%[0-9]+]] = load ptr, ptr %{{[0-9]+}}, align 8
// CIR-NEXT: [[VTABLE:%[0-9]+]] = load ptr, ptr [[THIS]], align 8
// CIR-NEXT: [[VBASE_OFFSET_PTR:%[0-9]+]] = getelementptr inbounds i8, ptr [[VTABLE]], i64 -12
// CIR-NEXT: [[VBASE_OFFSET:%[0-9]+]] = load i32, ptr [[VBASE_OFFSET_PTR]], align 4
// CIR-NEXT: [[VBASE_OFFSET_SEXT:%[0-9]+]] = sext i32 [[VBASE_OFFSET]] to i64
// CIR-NEXT: [[ADJ_THIS:%[0-9]+]] = getelementptr inbounds i8, ptr [[THIS]], i64 [[VBASE_OFFSET_SEXT]]
// CIR: tail call noundef i32 @_ZN7Derived1fEi(ptr noundef nonnull align 8 dereferenceable(16) [[ADJ_THIS]], i32 noundef

class Base {
public:
  virtual int f(int x);

private:
  long x;
};

class Derived : public virtual Base {
public:
  virtual int f(int x);

private:
  long y;
};

int Base::f(int x) { return x + 1; }
int Derived::f(int x) { return x + 2; }
