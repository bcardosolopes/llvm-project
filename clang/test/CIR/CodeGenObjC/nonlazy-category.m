// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

__attribute__((objc_root_class))
@interface A
@end

@implementation A
@end

@interface A (Cat)
- (void)test;
@end

__attribute__((objc_nonlazy_class))
@implementation A (Cat)
- (void)test {}
@end

// CHECK-DAG: @"_OBJC_$_CATEGORY_INSTANCE_METHODS_A_$_Cat" = internal global
// CHECK: @"_OBJC_$_CATEGORY_A_$_Cat" = internal global
// CHECK: @"OBJC_LABEL_CATEGORY_$" = private global [1 x ptr] [ptr @"_OBJC_$_CATEGORY_A_$_Cat"], section "__DATA,__objc_catlist,regular,no_dead_strip", align 8
// CHECK: @"OBJC_LABEL_NONLAZY_CATEGORY_$" = private global [1 x ptr] [ptr @"_OBJC_$_CATEGORY_A_$_Cat"], section "__DATA,__objc_nlcatlist,regular,no_dead_strip", align 8
