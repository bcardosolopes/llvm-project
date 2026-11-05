// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -Wno-objc-root-class -emit-llvm -fclangir -o - %s | FileCheck %s

// CHECK: @"OBJC_LABEL_CLASS_$" = private global [5 x ptr] [ptr @"OBJC_CLASS_$_A", ptr @"OBJC_CLASS_$_B", ptr @"OBJC_CLASS_$_C", ptr @"OBJC_CLASS_$_D", ptr @"OBJC_CLASS_$_E"], section "__DATA,__objc_classlist,regular,no_dead_strip", align 8
// CHECK-NOT: @"OBJC_LABEL_CLASS_$.1"
// CHECK: @"OBJC_LABEL_NONLAZY_CLASS_$" = private global [3 x ptr] [ptr @"OBJC_CLASS_$_A", ptr @"OBJC_CLASS_$_D", ptr @"OBJC_CLASS_$_E"], section "__DATA,__objc_nlclslist,regular,no_dead_strip", align 8
// CHECK: @"OBJC_LABEL_NONLAZY_CATEGORY_$" = private global [2 x ptr] [ptr @"_OBJC_$_CATEGORY_A_$_Cat", ptr @"_OBJC_$_CATEGORY_E_$_MyCat"], section "__DATA,__objc_nlcatlist,regular,no_dead_strip", align 8
// CHECK-LABEL: define internal void @"\01+[A(Cat) load]"
// CHECK-LABEL: define internal void @"\01-[B(Cat) load]"
// CHECK-LABEL: define internal void @"\01-[E(MyCat) load]"

@interface A @end
@implementation A
+(void) load {
}
@end

@interface A (Cat) @end
@implementation A (Cat)
+(void) load {
}
@end

@interface B @end
@implementation B
-(void) load {
}
@end

@interface B (Cat) @end
@implementation B (Cat)
-(void) load {
}
@end

@interface C : A @end
@implementation C
@end

__attribute__((objc_nonlazy_class))
@interface D @end

@implementation D @end

@interface E @end

__attribute__((objc_nonlazy_class))
@implementation E @end

__attribute__((objc_nonlazy_class))
@implementation E (MyCat)
-(void) load {
}
@end
