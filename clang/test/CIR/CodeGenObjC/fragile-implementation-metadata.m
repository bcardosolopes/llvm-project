// RUN: %clang_cc1 -triple i386-apple-darwin9 -fobjc-runtime=macosx-fragile-10.5 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

__attribute__((objc_root_class))
@interface A {
  int _ivar;
}
@property(assign) int ivar;
+ (void)fm0;
- (void)im0;
@end

@implementation A
@synthesize ivar = _ivar;
+ (void)fm0 {}
- (void)im0 {}
@end

// CHECK-DAG: @OBJC_CLASS_NAME_{{[0-9.]*}} = private unnamed_addr constant [2 x i8] c"A\00", section "__TEXT,__cstring,cstring_literals", align 1
// CHECK-DAG: @OBJC_METH_VAR_NAME_{{[0-9.]*}} = private unnamed_addr constant [4 x i8] c"fm0\00", section "__TEXT,__cstring,cstring_literals", align 1
// CHECK-DAG: @OBJC_METH_VAR_TYPE_{{[0-9.]*}} = private unnamed_addr constant {{.*}} section "__TEXT,__cstring,cstring_literals", align 1
// CHECK-DAG: @OBJC_CLASS_METHODS_A = private global {{.*}} section "__OBJC,__cls_meth,regular,no_dead_strip", align 4
// CHECK-DAG: @OBJC_INSTANCE_METHODS_A = private global {{.*}} section "__OBJC,__inst_meth,regular,no_dead_strip", align 4
// CHECK-DAG: @OBJC_INSTANCE_VARIABLES_A = private global {{.*}} section "__OBJC,__instance_vars,regular,no_dead_strip", align 4
// CHECK-DAG: @"_OBJC_$_PROP_LIST_A" = private global {{.*}} i32 8, i32 1, {{.*}} section "__OBJC,__property,regular,no_dead_strip", align 4
// CHECK-DAG: @OBJC_CLASSEXT_A = private global {{.*}} i32 12, ptr null, ptr @"_OBJC_$_PROP_LIST_A" }, section "__OBJC,__class_ext,regular,no_dead_strip", align 4
// CHECK: @OBJC_METACLASS_A = private global {{.*}} ptr @OBJC_CLASS_NAME_{{[0-9.]*}}, ptr null, ptr @OBJC_CLASS_NAME_{{[0-9.]*}}, i32 0, i32 2, i32 48
// CHECK: @OBJC_CLASS_A = private global {{.*}} ptr @OBJC_METACLASS_A, ptr null, ptr @OBJC_CLASS_NAME_{{[0-9.]*}}, i32 0, i32 1, i32 4
// CHECK: @OBJC_SYMBOLS = private global {{.*}} [1 x ptr] [ptr @OBJC_CLASS_A] }, section "__OBJC,__symbols,regular,no_dead_strip", align 4
// CHECK: @OBJC_MODULES = private global {{.*}} i32 7, i32 16, {{.*}} ptr @OBJC_SYMBOLS }, section "__OBJC,__module_info,regular,no_dead_strip", align 4
// CHECK-NOT: @"OBJC_CLASS_$_A"
// CHECK-NOT: @"OBJC_METACLASS_$_A"
