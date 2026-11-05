// RUN: %clang_cc1 -triple i386-apple-darwin9 -fobjc-runtime=macosx-fragile-10.5 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

@interface B
+ (void)im0;
@end

@interface C
+ (void)im1;
@end

void f0(void) {
  [B im0];
  [C im1];
}

// CHECK-DAG: @OBJC_CLASS_NAME_{{[0-9.]*}} = private unnamed_addr constant [2 x i8] c"B\00", section "__TEXT,__cstring,cstring_literals", align 1
// CHECK-DAG: @OBJC_CLASS_NAME_{{[0-9.]*}} = private unnamed_addr constant [2 x i8] c"C\00", section "__TEXT,__cstring,cstring_literals", align 1
// CHECK-DAG: @OBJC_CLASS_REFERENCES_{{[0-9.]*}} = private global ptr @OBJC_CLASS_NAME_{{[0-9.]*}}, section "__OBJC,__cls_refs,literal_pointers,no_dead_strip", align 4
// CHECK-DAG: @OBJC_CLASS_REFERENCES_{{[0-9.]*}} = private global ptr @OBJC_CLASS_NAME_{{[0-9.]*}}, section "__OBJC,__cls_refs,literal_pointers,no_dead_strip", align 4
// CHECK-DAG: @OBJC_SELECTOR_REFERENCES_{{[0-9.]*}} = private externally_initialized global ptr @OBJC_METH_VAR_NAME_{{[0-9.]*}}, section "__OBJC,__message_refs,literal_pointers,no_dead_strip", align 4
// CHECK-DAG: @OBJC_SELECTOR_REFERENCES_{{[0-9.]*}} = private externally_initialized global ptr @OBJC_METH_VAR_NAME_{{[0-9.]*}}, section "__OBJC,__message_refs,literal_pointers,no_dead_strip", align 4
// CHECK-NOT: @"OBJC_CLASS_$_B"
// CHECK-NOT: @"OBJC_CLASS_$_C"
// CHECK-NOT: @"OBJC_CLASSLIST_REFERENCES_$_"
// CHECK: define void @f0()
// CHECK: load ptr, ptr @OBJC_CLASS_REFERENCES_
// CHECK: load ptr, ptr @OBJC_CLASS_REFERENCES_.{{[0-9]+}}
