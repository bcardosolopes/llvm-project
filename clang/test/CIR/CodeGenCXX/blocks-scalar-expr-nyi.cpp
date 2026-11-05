// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s

// Ensure scalar captured block literals lower to stack blocks and recover the
// captured field inside the invoke body.

void side(void);
void f(void (^)(void));
void test(int x) {
  f(^{ if (x) side(); });
}

// CHECK: @_NSConcreteStackBlock = external global ptr
// CHECK: @___Z4testi_block_invoke.descriptor = internal constant { i64, i64, ptr, ptr } { i64 0, i64 36,
// CHECK-LABEL: define internal void @___Z4testi_block_invoke.invoke(ptr noundef %{{.*}})
// CHECK: %[[CAPADDR:.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, i32 }>, ptr %{{.*}}, i32 0, i32 5
// CHECK: %[[CAPLOAD:.*]] = load i32, ptr %[[CAPADDR]], align 8
// CHECK: icmp ne i32 %[[CAPLOAD]], 0
// CHECK: call void @_Z4sidev()
// CHECK-LABEL: define dso_local void @_Z4testi(i32 noundef %{{.*}})
// CHECK: %[[BLOCK:.*]] = alloca <{ ptr, i32, i32, ptr, ptr, i32 }>, i64 1, align 8
// CHECK: store ptr @_NSConcreteStackBlock, ptr %{{.*}}, align 8
// CHECK: store ptr @___Z4testi_block_invoke.invoke, ptr %{{.*}}, align 8
// CHECK: store ptr @___Z4testi_block_invoke.descriptor, ptr %{{.*}}, align 8
// CHECK: %[[CAPSLOT:.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, i32 }>, ptr %[[BLOCK]], i32 0, i32 5
// CHECK: %[[XLOAD:.*]] = load i32, ptr %{{.*}}, align 4
// CHECK: store i32 %[[XLOAD]], ptr %[[CAPSLOT]], align 8
// CHECK: call void @_Z1fU13block_pointerFvvE(ptr noundef %[[BLOCK]])
