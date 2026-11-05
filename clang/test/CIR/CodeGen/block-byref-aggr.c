// RUN: %clang_cc1 %s -fclangir -emit-llvm -o - -fblocks -triple x86_64-apple-darwin10 | FileCheck %s

typedef struct { int v; } Agg;
Agg makeAgg(void);

void test0(void) {
  __block Agg a = {100};
  ^{ (void)a; };

  a = makeAgg();
}

// CHECK-LABEL: define{{.*}} void @test0()
// CHECK: %[[TEMP:[0-9]+]] = alloca %{{.*}}Agg
// CHECK: %[[RESULT:[0-9]+]] = call i32 @makeAgg()
// CHECK: store i32 %[[RESULT]], ptr %{{.*}}
// CHECK: %[[A_FORWARDING:[0-9]+]] = getelementptr inbounds nuw {{.*}}, ptr %{{[0-9]+}}, i32 0, i32 1
// CHECK: %[[FORWARDED:[0-9]+]] = load ptr, ptr %[[A_FORWARDING]]
// CHECK: %[[FIELD:[0-9]+]] = getelementptr inbounds nuw {{.*}}, ptr %[[FORWARDED]], i32 0, i32 4
// CHECK: call void @llvm.memcpy{{.*}}(ptr align 8 %[[FIELD]], ptr align 4 %[[TEMP]], i64 4, i1 false)

void test1(void) {
  __block Agg a, b;
  ^{ (void)a; (void)b; };
  a = b = makeAgg();
}

// CHECK-LABEL: define{{.*}} void @test1()
// CHECK: %[[TEMP:[0-9]+]] = alloca %{{.*}}Agg
// CHECK: %[[RESULT:[0-9]+]] = call i32 @makeAgg()
// CHECK: store i32 %[[RESULT]], ptr %{{.*}}
// CHECK: %[[B_FORWARDING:[0-9]+]] = getelementptr inbounds nuw {{.*}}, ptr %{{[0-9]+}}, i32 0, i32 1
// CHECK: %[[B_FORWARDED:[0-9]+]] = load ptr, ptr %[[B_FORWARDING]]
// CHECK: %[[B_FIELD:[0-9]+]] = getelementptr inbounds nuw {{.*}}, ptr %[[B_FORWARDED]], i32 0, i32 4
// CHECK: call void @llvm.memcpy{{.*}}(ptr align 8 %[[B_FIELD]], ptr align 4 %[[TEMP]], i64 4, i1 false)
// CHECK: %[[A_FORWARDING:[0-9]+]] = getelementptr inbounds nuw {{.*}}, ptr %{{[0-9]+}}, i32 0, i32 1
// CHECK: %[[A_FORWARDED:[0-9]+]] = load ptr, ptr %[[A_FORWARDING]]
// CHECK: %[[A_FIELD:[0-9]+]] = getelementptr inbounds nuw {{.*}}, ptr %[[A_FORWARDED]], i32 0, i32 4
// CHECK: call void @llvm.memcpy{{.*}}(ptr align 8 %[[A_FIELD]], ptr align 4 %[[TEMP]], i64 4, i1 false)
