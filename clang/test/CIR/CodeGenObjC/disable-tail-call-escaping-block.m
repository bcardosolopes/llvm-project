// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fno-escaping-block-tail-calls -fclangir -o - %s | FileCheck %s

typedef void (^BlockTy)(void);

void use_int(int);
void callee1(BlockTy);

void test(id a) {
  BlockTy b0 = ^{ (void)a; use_int(3); };
  callee1(b0);
}

// CHECK-LABEL: define internal void @__test_block_invoke.invoke(ptr noundef %0) #0
// CHECK: call void @use_int(i32 noundef 3)

// CHECK-LABEL: define{{.*}} void @test(
// CHECK: call void @callee1(ptr noundef %{{.*}})
// CHECK: attributes #0 = {{.*}}"disable-tail-calls"="true"{{.*}}
