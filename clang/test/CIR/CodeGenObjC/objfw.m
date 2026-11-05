// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fobjc-runtime=objfw -emit-llvm -fclangir -o - %s | FileCheck %s

@interface Test0
+ (void)test;
@end

void test0(void) {
  [Test0 test];
}

// CHECK-DAG: @.objc_source_file_name = private unnamed_addr constant
// CHECK-DAG: @.objc_module = internal global { i64, i64, ptr, ptr } { i64 9, i64 32, ptr @.objc_source_file_name, ptr @.objc_symtab }, align 8
// CHECK: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @.objc_load_function, ptr null }]
// CHECK-LABEL: define{{.*}} void @test0()
// CHECK-NOT: @objc_lookup_class
// CHECK: [[LOOKUP:%.*]] = call ptr @objc_msg_lookup(ptr @_OBJC_CLASS_Test0, ptr {{.*}}), !GNUObjCMessageSend ![[MSG:[0-9]+]]
// CHECK-NEXT: call void [[LOOKUP]](ptr noundef @_OBJC_CLASS_Test0, ptr noundef {{.*}}), !GNUObjCMessageSend ![[MSG]]
// CHECK-LABEL: define internal void @.objc_load_function() {
// CHECK: call void (ptr, ...) @__objc_exec_class(ptr @.objc_module)
// CHECK: declare void @__objc_exec_class(ptr, ...)
// CHECK: ![[MSG]] = !{!"test", !"Test0", i1 true}
