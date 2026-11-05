// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s --check-prefix=FRAGILE-MODULE
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s --check-prefix=FRAGILE-FNS
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-10.14 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s --check-prefix=NONFRAGILE

typedef struct {
  long a, b, c;
} Big;

__attribute__((objc_root_class))
@interface Root
- (id)init;
@end

@interface Base : Root
+ (void)classMethod;
- (Big)big;
@end

@interface Derived : Base
@end

@implementation Derived
- (id)init {
  return [super init];
}

+ (void)classMethod {
  [super classMethod];
}

- (Big)big {
  return [super big];
}
@end

// FRAGILE-MODULE: @OBJC_CLASS_NAME_{{[0-9.]*}} = private unnamed_addr constant [5 x i8] c"Base\00", section "__TEXT,__cstring,cstring_literals", align 1
// FRAGILE-MODULE-NOT: OBJC_CLASSLIST_SUP_REFS_
// FRAGILE-MODULE: declare ptr @objc_msgSendSuper
// FRAGILE-MODULE: declare void @objc_msgSendSuper_stret
// FRAGILE-MODULE-NOT: @objc_msgSendSuper2
// FRAGILE-MODULE-NOT: @objc_msgSendSuper2_stret

// FRAGILE-FNS-LABEL: define internal ptr @"\01-[Derived init]"
// FRAGILE-FNS: store ptr @OBJC_CLASS_NAME_{{[0-9.]*}}, ptr %{{.*}}, align 8
// FRAGILE-FNS: call ptr @objc_msgSendSuper(ptr noundef %{{.*}}, ptr noundef %{{.*}})

// FRAGILE-FNS-LABEL: define internal void @"\01+[Derived classMethod]"
// FRAGILE-FNS: store ptr @OBJC_CLASS_NAME_{{[0-9.]*}}, ptr %{{.*}}, align 8
// FRAGILE-FNS: call void @objc_msgSendSuper(ptr noundef %{{.*}}, ptr noundef %{{.*}})

// FRAGILE-FNS-LABEL: define internal void @"\01-[Derived big]"
// FRAGILE-FNS: store ptr @OBJC_CLASS_NAME_{{[0-9.]*}}, ptr %{{.*}}, align 8
// FRAGILE-FNS: call void @objc_msgSendSuper_stret(ptr dead_on_unwind writable sret(%struct.Big) align 8 %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})

// NONFRAGILE: @"OBJC_CLASSLIST_SUP_REFS_$_" = private global ptr @"OBJC_CLASS_$_Derived", section "__DATA,__objc_superrefs,regular,no_dead_strip", align 8
// NONFRAGILE: declare ptr @objc_msgSendSuper2
// NONFRAGILE: declare void @objc_msgSendSuper2_stret
