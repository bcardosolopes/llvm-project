// RUN: %clang_cc1 -triple x86_64-apple-darwin -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s

typedef void (^BlockTy)(void);
void escaping(BlockTy);

struct S {
  id a;
  id b;
};

// CHECK: %struct.S = type { ptr, ptr }
// CHECK: @[[LAYOUT:.*]] = private unnamed_addr constant [2 x i8] c"a\00", align 1

// CHECK-LABEL: define{{.*}} void @test()
// CHECK: %[[BYREF:.+]] = alloca { ptr, ptr, i32, i32, ptr, %struct.S }, i64 1, align 8
// CHECK: %[[FLAGS_ADDR:.+]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, %struct.S }, ptr %[[BYREF]], i32 0, i32 2
// CHECK: store i32 268435456, ptr %[[FLAGS_ADDR]], align 8
// CHECK: %[[LAYOUT_ADDR:.+]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, %struct.S }, ptr %[[BYREF]], i32 0, i32 4
// CHECK: store ptr @[[LAYOUT]], ptr %[[LAYOUT_ADDR]], align 8

void test(void) {
  __block struct S s;
  escaping(^{ (void)s; });
}
