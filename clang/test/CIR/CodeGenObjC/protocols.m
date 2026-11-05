// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-10.14 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

@protocol P1
@property(readonly) id prop;
@property(class, readonly) int classProp;
- (void)methodReq0;
+ (void)classMethodReq0;
@optional
- (void)methodOpt1;
@end

@protocol P2
@end

@protocol P3 <P1, P2>
- (id<P1>)print0;
@end

void *foo(void) {
  return @protocol(P3);
}

// CHECK-DAG: @"_OBJC_$_PROTOCOL_INSTANCE_METHODS_P1" = internal global
// CHECK-DAG: @"_OBJC_$_PROTOCOL_CLASS_METHODS_P1" = internal global
// CHECK-DAG: @"_OBJC_$_PROTOCOL_INSTANCE_METHODS_OPT_P1" = internal global
// CHECK-DAG: @"_OBJC_$_PROP_LIST_P1" = internal global
// CHECK-DAG: @"_OBJC_$_PROTOCOL_REFS_P3" = internal global { i64, [3 x ptr] } { i64 2, [3 x ptr] [ptr @"_OBJC_PROTOCOL_$_P1", ptr @"_OBJC_PROTOCOL_$_P2", ptr null] }, section "__DATA, __objc_const", align 8
// CHECK-DAG: @"_OBJC_$_PROTOCOL_METHOD_TYPES_P3" = internal global [1 x ptr] [ptr @OBJC_METH_VAR_TYPE_{{.*}}], section "__DATA, __objc_const", align 8
// CHECK-DAG: @OBJC_METH_VAR_TYPE_{{.*}} = private unnamed_addr constant {{.*}} c"@{{.*}}<P1>{{.*}}", section "__TEXT,__objc_methtype,cstring_literals", align 1
// CHECK-DAG: @"_OBJC_PROTOCOL_$_P1" = weak hidden global
// CHECK-DAG: @"_OBJC_LABEL_PROTOCOL_$_P1" = weak hidden global ptr @"_OBJC_PROTOCOL_$_P1", section "__DATA,__objc_protolist,coalesced,no_dead_strip", align 8
// CHECK-DAG: @"_OBJC_PROTOCOL_$_P2" = weak hidden global
// CHECK-DAG: @"_OBJC_LABEL_PROTOCOL_$_P2" = weak hidden global ptr @"_OBJC_PROTOCOL_$_P2", section "__DATA,__objc_protolist,coalesced,no_dead_strip", align 8
// CHECK-DAG: @"_OBJC_PROTOCOL_$_P3" = weak hidden global
// CHECK-DAG: @"_OBJC_LABEL_PROTOCOL_$_P3" = weak hidden global ptr @"_OBJC_PROTOCOL_$_P3", section "__DATA,__objc_protolist,coalesced,no_dead_strip", align 8
// CHECK-DAG: @"_OBJC_PROTOCOL_REFERENCE_$_P3" = weak hidden global ptr @"_OBJC_PROTOCOL_$_P3", section "__DATA,__objc_protorefs,coalesced,no_dead_strip", align 8
// CHECK: @llvm.used = appending global
// CHECK-DAG: ptr @"_OBJC_PROTOCOL_$_P1"
// CHECK-DAG: ptr @"_OBJC_LABEL_PROTOCOL_$_P1"
// CHECK-DAG: ptr @"_OBJC_PROTOCOL_$_P2"
// CHECK-DAG: ptr @"_OBJC_LABEL_PROTOCOL_$_P2"
// CHECK-DAG: ptr @"_OBJC_PROTOCOL_$_P3"
// CHECK-DAG: ptr @"_OBJC_LABEL_PROTOCOL_$_P3"
// CHECK-DAG: ptr @"_OBJC_PROTOCOL_REFERENCE_$_P3"
// CHECK: @llvm.compiler.used = appending global
// CHECK-DAG: ptr @"_OBJC_$_PROTOCOL_INSTANCE_METHODS_P1"
// CHECK-DAG: ptr @"_OBJC_$_PROTOCOL_CLASS_METHODS_P1"
// CHECK-DAG: ptr @"_OBJC_$_PROTOCOL_INSTANCE_METHODS_OPT_P1"
// CHECK-DAG: ptr @"_OBJC_$_PROP_LIST_P1"
// CHECK-DAG: ptr @"_OBJC_$_PROTOCOL_REFS_P3"
// CHECK-DAG: ptr @"_OBJC_$_PROTOCOL_METHOD_TYPES_P3"
// CHECK: define ptr @foo()
// CHECK: load ptr, ptr @"_OBJC_PROTOCOL_REFERENCE_$_P3", align 8
