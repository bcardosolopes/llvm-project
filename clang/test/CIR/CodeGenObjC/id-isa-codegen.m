// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 -emit-llvm -fclangir -o - %s | FileCheck %s

typedef struct objc_class *Class;

typedef struct objc_object {
  Class isa;
} *id;

@interface I
+ (Class)class;
- (void)meth:(id)object :(id)src_object;
+ (unsigned char)isSubclassOfClass:(Class)aClass;
@end

@implementation I
+ (Class)class { return 0; }
+ (unsigned char)isSubclassOfClass:(Class)aClass { return 0; }
- (void)meth:(id)object :(id)src_object {
  [object->isa isSubclassOfClass:[I class]];

  [(*object).isa isSubclassOfClass:[I class]];

  object->isa = src_object->isa;
  (*src_object).isa = (*object).isa;
}
@end

// CHECK-DAG: @OBJC_CLASS_NAME_ = private unnamed_addr constant [2 x i8] c"I\00", section "__TEXT,__cstring,cstring_literals", align 1
// CHECK-DAG: @OBJC_CLASS_REFERENCES_ = private global ptr @OBJC_CLASS_NAME_, section "__OBJC,__cls_refs,literal_pointers,no_dead_strip", align 8
// CHECK-DAG: @[[CLASS_SEL_NAME:OBJC_METH_VAR_NAME_(\.[0-9]+)?]] = private unnamed_addr constant [6 x i8] c"class\00", section "__TEXT,__cstring,cstring_literals", align 1
// CHECK-DAG: @[[CLASS_SEL_REF:OBJC_SELECTOR_REFERENCES_(\.[0-9]+)?]] = private externally_initialized global ptr @[[CLASS_SEL_NAME]], section "__OBJC,__message_refs,literal_pointers,no_dead_strip", align 8
// CHECK-DAG: @[[SUBCLASS_SEL_NAME:OBJC_METH_VAR_NAME_(\.[0-9]+)?]] = private unnamed_addr constant [19 x i8] c"isSubclassOfClass:\00", section "__TEXT,__cstring,cstring_literals", align 1
// CHECK-DAG: @[[SUBCLASS_SEL_REF:OBJC_SELECTOR_REFERENCES_(\.[0-9]+)?]] = private externally_initialized global ptr @[[SUBCLASS_SEL_NAME]], section "__OBJC,__message_refs,literal_pointers,no_dead_strip", align 8
// CHECK-LABEL: define internal void @"\01-[I meth::]"
// CHECK: store ptr %2, ptr %[[OBJECT_SLOT:[0-9]+]], align 8
// CHECK: store ptr %3, ptr %[[SRC_SLOT:[0-9]+]], align 8
// CHECK: %[[OBJECT:.*]] = load ptr, ptr %[[OBJECT_SLOT]], align 8
// CHECK-NEXT: %[[ISA:.*]] = load ptr, ptr %[[OBJECT]], align 8
// CHECK: call zeroext i8 @objc_msgSend(ptr noundef %[[ISA]]
// CHECK: %[[STORE1_SRC_OBJECT:.*]] = load ptr, ptr %[[SRC_SLOT]], align 8
// CHECK-NEXT: %[[STORE1_SRC_ISA:.*]] = load ptr, ptr %[[STORE1_SRC_OBJECT]], align 8
// CHECK: %[[STORE1_DST_OBJECT:.*]] = load ptr, ptr %[[OBJECT_SLOT]], align 8
// CHECK-NEXT: store ptr %[[STORE1_SRC_ISA]], ptr %[[STORE1_DST_OBJECT]], align 8
// CHECK: %[[STORE2_SRC_OBJECT:.*]] = load ptr, ptr %[[OBJECT_SLOT]], align 8
// CHECK-NEXT: %[[STORE2_SRC_ISA:.*]] = load ptr, ptr %[[STORE2_SRC_OBJECT]], align 8
// CHECK: %[[STORE2_DST_OBJECT:.*]] = load ptr, ptr %[[SRC_SLOT]], align 8
// CHECK-NEXT: store ptr %[[STORE2_SRC_ISA]], ptr %[[STORE2_DST_OBJECT]], align 8
