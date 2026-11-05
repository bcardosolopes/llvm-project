// RUN: %clang_cc1 -triple i386-apple-darwin9 -fobjc-runtime=macosx-fragile-10.5 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

@protocol P
+ (void)protoClass;
- (void)protoInst;
@end

__attribute__((objc_root_class))
@interface A <P>
+ (void)classMethod;
- (void)instMethod;
@end

@implementation A
+ (void)classMethod {}
- (void)instMethod {}
+ (void)protoClass {}
- (void)protoInst {}
@end

@interface A (Cat) <P>
+ (void)catClassMethod;
- (void)catInstMethod;
@end

@implementation A (Cat)
+ (void)catClassMethod {}
- (void)catInstMethod {}
@end

@interface J0
@end

@implementation J0 (Category)
@end

// CHECK-DAG: @_OBJC_PROTOCOLEXT_P = internal global {{.*}}, align 4
// CHECK-DAG: @OBJC_PROTOCOL_INSTANCE_METHODS_P = private global {{.*}} section "__OBJC,__cat_inst_meth,regular,no_dead_strip", align 4
// CHECK-DAG: @OBJC_PROTOCOL_CLASS_METHODS_P = private global {{.*}} section "__OBJC,__cat_cls_meth,regular,no_dead_strip", align 4
// CHECK-DAG: @OBJC_PROTOCOL_P = private global { ptr, ptr, ptr, ptr, ptr } { ptr @_OBJC_PROTOCOLEXT_P, ptr @OBJC_CLASS_NAME_{{[0-9.]*}}, ptr null, ptr @OBJC_PROTOCOL_INSTANCE_METHODS_P, ptr @OBJC_PROTOCOL_CLASS_METHODS_P }, section "__OBJC,__protocol,regular,no_dead_strip", align 4
// CHECK: @OBJC_CLASS_PROTOCOLS_A = private global { ptr, i32, [2 x ptr] } { ptr null, i32 1, [2 x ptr] [ptr @OBJC_PROTOCOL_P, ptr null] }, section "__OBJC,__cat_cls_meth,regular,no_dead_strip", align 4
// CHECK: @OBJC_METACLASS_A = private global {{.*}} ptr @OBJC_CLASS_METHODS_A, ptr null, ptr @OBJC_CLASS_PROTOCOLS_A, ptr null{{.*}} section "__OBJC,__meta_class,regular,no_dead_strip", align 4
// CHECK: @OBJC_CLASS_A = private global {{.*}} ptr @OBJC_INSTANCE_METHODS_A, ptr null, ptr @OBJC_CLASS_PROTOCOLS_A, ptr null{{.*}} section "__OBJC,__class,regular,no_dead_strip", align 4
// CHECK-DAG: @OBJC_CATEGORY_INSTANCE_METHODS_A_Cat = private global {{.*}} section "__OBJC,__cat_inst_meth,regular,no_dead_strip", align 4
// CHECK-DAG: @OBJC_CATEGORY_CLASS_METHODS_A_Cat = private global {{.*}} section "__OBJC,__cat_cls_meth,regular,no_dead_strip", align 4
// CHECK: @OBJC_CATEGORY_PROTOCOLS_A_Cat = private global { ptr, i32, [2 x ptr] } { ptr null, i32 1, [2 x ptr] [ptr @OBJC_PROTOCOL_P, ptr null] }, section "__OBJC,__cat_cls_meth,regular,no_dead_strip", align 4
// CHECK: @OBJC_CATEGORY_A_Cat = private global {{.*}} ptr @OBJC_CATEGORY_INSTANCE_METHODS_A_Cat, ptr @OBJC_CATEGORY_CLASS_METHODS_A_Cat, ptr @OBJC_CATEGORY_PROTOCOLS_A_Cat, i32 32, ptr null, ptr null }, section "__OBJC,__category,regular,no_dead_strip", align 4
// CHECK-DAG: @OBJC_CATEGORY_J0_Category = private global {{.*}} section "__OBJC,__category,regular,no_dead_strip", align 4
// CHECK: @OBJC_SYMBOLS = private global { i32, ptr, i16, i16, [3 x ptr] } { i32 0, ptr null, i16 1, i16 2, [3 x ptr] [ptr @OBJC_CLASS_A, ptr @OBJC_CATEGORY_A_Cat, ptr @OBJC_CATEGORY_J0_Category] }, section "__OBJC,__symbols,regular,no_dead_strip", align 4
// CHECK: @OBJC_MODULES = private global { i32, i32, ptr, ptr } { i32 7, i32 16, ptr @OBJC_CLASS_NAME_{{[0-9.]*}}, ptr @OBJC_SYMBOLS }, section "__OBJC,__module_info,regular,no_dead_strip", align 4
