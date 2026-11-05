// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-runtime=gcc -fclangir -o - %s | FileCheck --check-prefix=GCC %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-runtime=gnustep-1.5 -fclangir -o - %s | FileCheck --check-prefix=GCC %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-runtime=gnustep-1.6 -fclangir -o - %s | FileCheck --check-prefix=GNUSTEP %s

@interface helloclass {
@private int varName;
}
@property (readwrite, assign) int propName;
@end

@implementation helloclass
@synthesize propName = varName;
@end

// GCC-DAG: @.objc_property_list = internal global { i32, ptr, [1 x { ptr, i8, i8, i8, i8, ptr, ptr, ptr, ptr }] }
// GCC-DAG: @__objc_class_name_helloclass = global i64 0, align 8
// GCC-NOT: Ti,VvarName
// GCC: @_OBJC_CLASS_helloclass = global

// GNUSTEP-DAG: @__objc_ivar_offset_helloclass.varName = global ptr getelementptr (i8, ptr @.objc_ivar_list, i64 24), align 8
// GNUSTEP-DAG: @__objc_ivar_offset_value_helloclass.varName = global i32 0, align 4
// GNUSTEP-DAG: @.objc_property_list = internal global { i32, ptr, [1 x { ptr, i8, i8, i8, i8, ptr, ptr, ptr, ptr }] }
// GNUSTEP-DAG: @_OBJC_METACLASS_helloclass = global {{.*}} i64 0, i64 0 }, align 8
// GNUSTEP-DAG: Ti,VvarName
// GNUSTEP: define internal i32 @_i_helloclass__propName(ptr noundef %0, ptr noundef %1)
// GNUSTEP: load ptr, ptr @__objc_ivar_offset_helloclass.varName, align 8
// GNUSTEP: load atomic i32, {{.*}} unordered
// GNUSTEP: define internal void @_i_helloclass__setPropName_(ptr noundef %0, ptr noundef %1, i32 noundef %2)
// GNUSTEP: store atomic i32 {{.*}} unordered
