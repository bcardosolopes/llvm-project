// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fclangir -o - %s | FileCheck %s

__attribute__((objc_root_class))
@interface NSObject
+ (void)class;
@end

__attribute__((objc_root_class, weak_import))
@interface WeakRootClass
@end

__attribute__((weak_import))
@interface WeakClass : WeakRootClass
@end

@interface MySubclass : WeakClass
@end

@implementation MySubclass
@end

__attribute__((weak_import))
@interface WeakClass3 : WeakRootClass
+ (void)message;
@end

void useWeakClass3(void) {
  [WeakClass3 message];
}

__attribute__((weak_import))
@interface NSURLQueryItem : NSObject
@end

@implementation NSURLQueryItem (Cat)
+ (void)classMethod {
  [super class];
}
@end

__attribute__((objc_root_class, weak_import))
@interface Root
@end

@interface Sub : Root
@end

@implementation Sub
@end

@implementation Root
@end

// CHECK-DAG: @"OBJC_METACLASS_$_WeakRootClass" = extern_weak global i8
// CHECK-DAG: @"OBJC_CLASS_$_WeakClass" = extern_weak global i8
// CHECK-DAG: @"OBJC_METACLASS_$_WeakClass" = extern_weak global i8
// CHECK-DAG: @"OBJC_CLASS_$_WeakClass3" = extern_weak global i8
// CHECK-DAG: @"OBJC_CLASS_$_NSURLQueryItem" = extern_weak global i8
// CHECK-DAG: @"OBJC_METACLASS_$_NSURLQueryItem" = extern_weak global i8
// CHECK-DAG: @"OBJC_CLASS_$_Root" = global { ptr, ptr, ptr, ptr, ptr }
// CHECK-DAG: @"OBJC_METACLASS_$_Root" = global { ptr, ptr, ptr, ptr, ptr }
