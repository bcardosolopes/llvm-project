// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -Wno-objc-root-class -fclangir -emit-llvm -o - %s | FileCheck %s

typedef const struct objc_selector *SEL;

@interface NSObject @end
@class NSString;

int __CFConstantStringClassReference[24];

@interface Bar : NSObject
+ (void)format:(NSString *)format, ...;
@end

SEL get_selector(void) {
  return @selector(format:);
}

void call_format(void) {
  [Bar format:@" "];
}

// CHECK-DAG: @OBJC_METH_VAR_NAME_ = private unnamed_addr constant [8 x i8] c"format:\00", section "__TEXT,__objc_methname,cstring_literals", align 1
// CHECK-DAG: @OBJC_SELECTOR_REFERENCES_ = internal externally_initialized global ptr @OBJC_METH_VAR_NAME_, section "__DATA,__objc_selrefs,literal_pointers,no_dead_strip", align 8
// CHECK-DAG: @"OBJC_CLASS_$_Bar" = external global i8
// CHECK-DAG: @"OBJC_CLASSLIST_REFERENCES_$_" = internal global ptr @"OBJC_CLASS_$_Bar", section "__DATA,__objc_classrefs,regular,no_dead_strip", align 8
// CHECK-DAG: @llvm.compiler.used = appending global [3 x ptr] [ptr @OBJC_METH_VAR_NAME_, ptr @OBJC_SELECTOR_REFERENCES_, ptr @"OBJC_CLASSLIST_REFERENCES_$_"], section "llvm.metadata"
// CHECK-LABEL: define ptr @get_selector()
// CHECK: %[[SEL:.*]] = load ptr, ptr @OBJC_SELECTOR_REFERENCES_, align 8, !invariant.load ![[INV:[0-9]+]]
// CHECK: ret ptr %[[SEL]]
// CHECK-LABEL: define void @call_format()
// CHECK: %[[CLS:.*]] = load ptr, ptr @"OBJC_CLASSLIST_REFERENCES_$_", align 8
// CHECK: %[[SEL2:.*]] = load ptr, ptr @OBJC_SELECTOR_REFERENCES_, align 8, !invariant.load ![[INV]]
// CHECK: call void @objc_msgSend(ptr noundef %[[CLS]], ptr noundef %[[SEL2]], ptr noundef @_unnamed_cfstring_{{.*}})
// CHECK: ret void
// CHECK: ![[INV]] = !{}
