// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -Wno-objc-root-class -emit-llvm -fclangir -o - %s | FileCheck %s

__attribute__((objc_root_class))
__attribute__((objc_class_stub))
__attribute__((objc_subclassing_restricted))
@interface Base
+ (void)classMethod;
- (void)instanceMethod;
@end

__attribute__((objc_class_stub))
__attribute__((objc_subclassing_restricted))
@interface Derived : Base
@end

@interface Derived (MyCategory)
+ (void)testClass;
- (void)testInstance;
@end

int main(void) {
  [Base classMethod];
  return 0;
}

@implementation Derived (MyCategory)
+ (void)testClass {
  [super classMethod];
}

- (void)testInstance {
  [super instanceMethod];
}
@end

// CHECK: @"OBJC_CLASSLIST_REFERENCES_$_" = internal global ptr getelementptr (i8, ptr @"OBJC_CLASS_$_Base", i64 1)
// CHECK: @"OBJC_CLASSLIST_SUP_REFS_$_.1" = private global ptr getelementptr (i8, ptr @"OBJC_CLASS_$_Derived", i64 1)
// CHECK-DAG: @"_OBJC_$_CATEGORY_INSTANCE_METHODS_Derived_$_MyCategory" = internal global
// CHECK-DAG: @"_OBJC_$_CATEGORY_CLASS_METHODS_Derived_$_MyCategory" = internal global
// CHECK: @"_OBJC_$_CATEGORY_Derived_$_MyCategory" = internal global {{.*}} ptr @"OBJC_CLASS_$_Derived", ptr @"_OBJC_$_CATEGORY_INSTANCE_METHODS_Derived_$_MyCategory", ptr @"_OBJC_$_CATEGORY_CLASS_METHODS_Derived_$_MyCategory"
// CHECK-NOT: @"OBJC_LABEL_CATEGORY_$"
// CHECK: @"OBJC_LABEL_STUB_CATEGORY_$" = private global [1 x ptr] [ptr @"_OBJC_$_CATEGORY_Derived_$_MyCategory"], section "__DATA,__objc_catlist2,regular,no_dead_strip", align 8
// CHECK: ; Function Attrs: nounwind nonlazybind willreturn memory(none)
// CHECK: declare extern_weak ptr @objc_loadClassref(ptr) #{{[0-9]+}}

// CHECK-LABEL: define i32 @main()
// CHECK: [[CLASS:%.*]] = call ptr @objc_loadClassref(ptr @"OBJC_CLASSLIST_REFERENCES_$_")
// CHECK: call void @objc_msgSend(ptr noundef [[CLASS]], ptr noundef

// CHECK-LABEL: define internal void @"\01+[Derived(MyCategory) testClass]"
// CHECK-NOT: @objc_loadClassref
// CHECK: load ptr, ptr @"OBJC_CLASSLIST_SUP_REFS_$_"
// CHECK: call void @objc_msgSendSuper2

// CHECK-LABEL: define internal void @"\01-[Derived(MyCategory) testInstance]"
// CHECK: [[SUPERCLASS:%.*]] = call ptr @objc_loadClassref(ptr @"OBJC_CLASSLIST_SUP_REFS_$_.1")
// CHECK: call void @objc_msgSendSuper2
