// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -emit-llvm -fobjc-runtime=gnustep-2.2 -fclangir -o - %s | FileCheck %s

@interface X
@end

@implementation X
- (int)x __attribute__((objc_direct)) { return 12; }

// CHECK-LABEL: define dso_local i32 @_i_X__x
// CHECK: icmp eq ptr

+ (int)clsMeth __attribute__((objc_direct)) { return 42; }
// CHECK-LABEL: define dso_local i32 @_c_X__clsMeth
// CHECK-NOT: call ptr @objc_msgSend
// CHECK: load i64, ptr %{{.*}}, align 64
// CHECK: and i64 %{{.*}}, 256
// CHECK: call void @objc_send_initialize(ptr %{{.*}})
// CHECK-DAG: declare void @objc_send_initialize(ptr)

@end

void callCls(void)
{
	// CHECK: call i32 @_c_X__clsMeth
	[X clsMeth];
}

void callInstance(X *x)
{
	// CHECK: call i32 @_i_X__x
	[x x];
}
