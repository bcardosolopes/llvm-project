// RUN: %clang_cc1 %s -fblocks -triple x86_64-apple-darwin -std=c++11 -fclangir -emit-llvm -o - | FileCheck %s

namespace test5 {
struct A {
  unsigned afield;
  A();
  A(const A &);
  ~A();
  void foo() const;
};

void doWithBlock(void (^)());

void test(bool cond) {
  A x;
  void (^b)() = (cond ? ^{ x.foo(); } : (void (^)())0);
  doWithBlock(b);
}

// CHECK-LABEL: define void @_ZN5test54testEb(
// CHECK: [[X:%.*]] = alloca %"struct.test5::A"
// CHECK: [[B:%.*]] = alloca ptr
// CHECK: [[BLOCK:%.*]] = alloca <{ ptr, i32, i32, ptr, ptr, %"struct.test5::A" }>
// CHECK: [[ACTIVE:%.*]] = alloca i8
// CHECK: [[ACTIVE2:%.*]] = alloca i8
// CHECK: [[CAPTURE:%.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, %"struct.test5::A" }>, ptr [[BLOCK]], i32 0, i32 5
// CHECK: call void @_ZN5test51AC1Ev(ptr noundef nonnull align 4 dereferenceable(4) [[X]])
// CHECK: br i1 %{{.*}}, label %[[TRUE:[0-9]+]], label %[[FALSE:[0-9]+]]
// CHECK: [[TRUE]]:
// CHECK: call void @_ZN5test51AC1ERKS0_(ptr noundef nonnull align 4 dereferenceable(4) [[CAPTURE]], ptr noundef nonnull align 4 dereferenceable(4) [[X]])
// CHECK: store i8 1, ptr [[ACTIVE]]
// CHECK: store i8 1, ptr [[ACTIVE2]]
// CHECK: br label %[[MERGE:[0-9]+]]
// CHECK: [[FALSE]]:
// CHECK: br label %[[MERGE]]
// CHECK: [[MERGE]]:
// CHECK: [[BLOCK_OR_NULL:%.*]] = phi ptr [ null, %[[FALSE]] ], [ [[BLOCK]], %[[TRUE]] ]
// CHECK: phi ptr
// CHECK: store ptr %{{.*}}, ptr [[B]]
// CHECK: [[LOADED_B:%.*]] = load ptr, ptr [[B]]
// CHECK: call void @_ZN5test511doWithBlockEU13block_pointerFvvE(ptr noundef [[LOADED_B]])
// CHECK: [[ACTIVE_VAL:%.*]] = load i8, ptr [[ACTIVE2]]
// CHECK: [[ACTIVE_BOOL:%.*]] = trunc i8 [[ACTIVE_VAL]] to i1
// CHECK: br i1 [[ACTIVE_BOOL]], label %[[DESTROY:[0-9]+]], label %[[EXIT:[0-9]+]]
// CHECK: [[DESTROY]]:
// CHECK: call void @_ZN5test51AD1Ev(ptr noundef nonnull align 4 dereferenceable(4) [[CAPTURE]])
// CHECK: br label %[[EXIT]]
// CHECK: [[EXIT]]:
// CHECK: call void @_ZN5test51AD1Ev(ptr noundef nonnull align 4 dereferenceable(4) [[X]])
} // namespace test5
