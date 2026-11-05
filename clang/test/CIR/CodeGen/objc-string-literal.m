// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-arc -fclangir -emit-llvm -o %t.cir.ll %s
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-arc -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

void NSLog(id, ...);

id global_string = @"global";

void f(void) {
  NSLog(@"hello");
}

// CIR: @__CFConstantStringClassReference = external global [0 x i32]
// CIR: @[[GLOBAL_BYTES:.*]] = private unnamed_addr constant [7 x i8] c"global\00", section "__TEXT,__cstring,cstring_literals", align 1
// CIR: @[[GLOBAL_CFSTR:.*]] = private global %struct.__NSConstantString_tag { ptr @__CFConstantStringClassReference, i32 1992, ptr @[[GLOBAL_BYTES]], i64 6 }, section "__DATA,__cfstring", align 8 #[[ARC_INERT:[0-9]+]]
// CIR: @global_string = global ptr @[[GLOBAL_CFSTR]], align 8
// CIR: @[[HELLO_BYTES:.*]] = private unnamed_addr constant [6 x i8] c"hello\00", section "__TEXT,__cstring,cstring_literals", align 1
// CIR: @[[HELLO_CFSTR:.*]] = private global %struct.__NSConstantString_tag { ptr @__CFConstantStringClassReference, i32 1992, ptr @[[HELLO_BYTES]], i64 5 }, section "__DATA,__cfstring", align 8 #[[ARC_INERT]]
// CIR-LABEL: define void @f()
// CIR: call void (ptr, ...) @NSLog(ptr noundef @[[HELLO_CFSTR]])
// CIR: attributes #[[ARC_INERT]] = { "objc_arc_inert" }

// OGCG: @__CFConstantStringClassReference = external global [0 x i32]
// OGCG: @[[GLOBAL_BYTES:.*]] = private unnamed_addr constant [7 x i8] c"global\00", section "__TEXT,__cstring,cstring_literals", align 1
// OGCG: @[[GLOBAL_CFSTR:.*]] = private global %struct.__NSConstantString_tag { ptr @__CFConstantStringClassReference, i32 1992, ptr @[[GLOBAL_BYTES]], i64 6 }, section "__DATA,__cfstring", align 8 #[[ARC_INERT:[0-9]+]]
// OGCG: @global_string = global ptr @[[GLOBAL_CFSTR]], align 8
// OGCG: @[[HELLO_BYTES:.*]] = private unnamed_addr constant [6 x i8] c"hello\00", section "__TEXT,__cstring,cstring_literals", align 1
// OGCG: @[[HELLO_CFSTR:.*]] = private global %struct.__NSConstantString_tag { ptr @__CFConstantStringClassReference, i32 1992, ptr @[[HELLO_BYTES]], i64 5 }, section "__DATA,__cfstring", align 8 #[[ARC_INERT]]
// OGCG-LABEL: define void @f()
// OGCG: call void (ptr, ...) @NSLog(ptr noundef @[[HELLO_CFSTR]])
// OGCG: attributes #[[ARC_INERT]] = { "objc_arc_inert" }
