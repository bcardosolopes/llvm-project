// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fobjc-runtime=gnustep-2.0 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s

@protocol X
@optional
- (id)x;
@required
+ (void *)y;
@property int reqProp;
@optional
@property int optProp;
@end

void *x(void) {
  return @protocol(X);
}

// CHECK-DAG: @[[PROTO_LIST:.*]] = internal global { ptr, i64, [0 x ptr] } { ptr null, i64 0, [0 x ptr] {{(undef|zeroinitializer)}} }, align 8
// CHECK-DAG: @[[REQ_INST:.*]] = internal global { i32, i32, [2 x { ptr, ptr }] } { i32 2, i32 16,
// CHECK-DAG: @[[REQ_CLASS:.*]] = internal global { i32, i32, [1 x { ptr, ptr }] } { i32 1, i32 16,
// CHECK-DAG: @[[OPT_INST:.*]] = internal global { i32, i32, [3 x { ptr, ptr }] } { i32 3, i32 16,
// CHECK-DAG: @[[OPT_CLASS:.*]] = internal global { i32, i32, [0 x { ptr, ptr }] } { i32 0, i32 16, [0 x { ptr, ptr }] {{(undef|zeroinitializer)}} }, align 8
// CHECK-DAG: @[[REQ_PROP:.*]] = internal global { i32, i32, ptr, [1 x { ptr, ptr, ptr, ptr, ptr }] } { i32 1, i32 40, ptr null,
// CHECK-DAG: @[[OPT_PROP:.*]] = internal global { i32, i32, ptr, [1 x { ptr, ptr, ptr, ptr, ptr }] } { i32 1, i32 40, ptr null,
// CHECK-DAG: @._OBJC_PROTOCOL_X = global { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr } { ptr inttoptr (i{{32|64}} 4 to ptr), ptr {{.*}}, ptr @[[PROTO_LIST]], ptr @[[REQ_INST]], ptr @[[REQ_CLASS]], ptr @[[OPT_INST]], ptr @[[OPT_CLASS]], ptr @[[REQ_PROP]], ptr @[[OPT_PROP]], ptr null, ptr null }, section "__objc_protocols", comdat, align 8
// CHECK-DAG: @._OBJC_REF_PROTOCOL_X = linkonce_odr global ptr @._OBJC_PROTOCOL_X, section "__objc_protocol_refs", comdat, align 8
// CHECK-DAG: @.objc_null_cls_init_ref = linkonce_odr hidden global { ptr } zeroinitializer, section "__objc_classes", comdat, align 8
// CHECK-DAG: @.objc_null_class_ref = linkonce_odr hidden global { ptr, ptr } zeroinitializer, section "__objc_class_refs", comdat, align 8
// CHECK-DAG: @llvm.used = appending global {{.*}}ptr @.objc_null_cls_init_ref{{.*}}ptr @.objc_null_class_ref{{.*}}
// CHECK: define dso_local ptr @x()
// CHECK: load ptr, ptr @._OBJC_REF_PROTOCOL_X, align 8
