// RUN: %clang_cc1 %s -fblocks -triple x86_64-apple-darwin -std=c++11 -fclangir -emit-llvm -o - | FileCheck %s

template <class T> void takeItByValue(T);
void takeABlock(void (^)());

namespace test_int_ref {
void test() {
  const int y = 200;
  const int &x = y;
  takeABlock(^{ takeItByValue(x); });
  // CHECK: call void @_Z13takeItByValueIiEvT_(i32 noundef %{{.*}})
}
} // namespace test_int_ref

namespace test_float_ref {
void test() {
  const float y = 100;
  const float &x = y;
  takeABlock(^{ takeItByValue(x); });
  // CHECK: call void @_Z13takeItByValueIfEvT_(float noundef %{{.*}})
}
} // namespace test_float_ref

namespace test_block_in_lambda {
void takeBlock(void (^)());

struct A {
  void *p;
  A(const A &);
  ~A();
  void use() const;
};

void test(A a) {
  auto lambda = [a]() {
    takeBlock(^{ a.use(); });
  };
  lambda();
}

// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32c30_ZTSN20test_block_in_lambda1AE(
// CHECK: [[COPY_DST:%.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, %"struct.test_block_in_lambda::A" }, ptr %0, i32 0, i32 5
// CHECK-NEXT: [[COPY_SRC:%.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, %"struct.test_block_in_lambda::A" }, ptr %1, i32 0, i32 5
// CHECK-NEXT: call void @_ZN20test_block_in_lambda1AC1ERKS0_(ptr noundef nonnull align 8 dereferenceable(8) [[COPY_DST]], ptr noundef nonnull align 8 dereferenceable(8) [[COPY_SRC]])

// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32c30_ZTSN20test_block_in_lambda1AE(
// CHECK: [[DESTROY_FIELD:%.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, %"struct.test_block_in_lambda::A" }, ptr %0, i32 0, i32 5
// CHECK-NEXT: call void @_ZN20test_block_in_lambda1AD1Ev(ptr noundef nonnull align 8 dereferenceable(8) [[DESTROY_FIELD]])

// CHECK-LABEL: define internal void @"_ZZN20test_block_in_lambda4testENS_1AEENK3$_0clEv"(
// CHECK: [[BLOCK:%.*]] = alloca { ptr, i32, i32, ptr, ptr, %"struct.test_block_in_lambda::A" }, i64 1, align 8
// The block storage and the captured-field address are materialized at the
// function entry so the enclosing-scope destructor dominates them; the capture
// is still copy-constructed before, and destroyed after, the block is used.
// CHECK: [[BLOCK_CAPTURED:%.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, %"struct.test_block_in_lambda::A" }, ptr [[BLOCK]], i32 0, i32 5
// CHECK: [[LAMBDA_CAPTURED:%.*]] = getelementptr inbounds nuw %class.anon.0, ptr %{{.*}}, i32 0, i32 0
// CHECK-NEXT: call void @_ZN20test_block_in_lambda1AC1ERKS0_(ptr noundef nonnull align 8 dereferenceable(8) [[BLOCK_CAPTURED]], ptr noundef nonnull align 8 dereferenceable(8) [[LAMBDA_CAPTURED]])
// CHECK: call void @_ZN20test_block_in_lambda9takeBlockEU13block_pointerFvvE(ptr noundef [[BLOCK]])
// CHECK: call void @_ZN20test_block_in_lambda1AD1Ev(ptr noundef nonnull align 8 dereferenceable(8) [[BLOCK_CAPTURED]])
} // namespace test_block_in_lambda
