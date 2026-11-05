// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fobjc-arc -emit-llvm -fobjc-runtime=gnustep-2.0 -fclangir -o - %s | FileCheck %s

typedef struct {
  int x[12];
} Big;

@protocol P
- (Big) foo;
- (Big) fooConsuming: (__attribute__((ns_consumed)) id)arg;
- (_Complex float) complex;
@end

@interface SuperClass
- (Big) foo;
@end

@interface TestClass : SuperClass
- (void)test_self_send;
- (void)test_super_send;
@end

@implementation TestClass
// CHECK-LABEL: define{{.*}} void @_i_TestClass__test_self_send
// CHECK-NOT:   icmp
// CHECK:       @objc_msg_lookup_sender
// CHECK:       call void %{{.*}}(ptr dead_on_unwind writable sret(%struct.Big) align 4 %{{.*}}, ptr noundef %{{.*}}, ptr noundef @".objc_selector_foo_{{.*}}"), !GNUObjCMessageSend ![[FOO_MD:[0-9]+]]
- (void)test_self_send {
  Big big = [self foo];
}

// CHECK-LABEL: define{{.*}} void @_i_TestClass__test_super_send
// CHECK-NOT:   icmp
// CHECK:       @objc_msg_lookup_super({{.*}}) #[[SUPER_LOOKUP_ATTR:[0-9]+]]
// CHECK:       call void %{{.*}}(ptr dead_on_unwind writable sret(%struct.Big) align 4 %{{.*}}, ptr noundef %{{.*}}, ptr noundef @.objc_selector_foo_), !GNUObjCMessageSend ![[SUPER_FOO_MD:[0-9]+]]
- (void)test_super_send {
  Big big = [super foo];
}
@end

// CHECK-LABEL: define{{.*}} void @test_loop_zeroing(
// CHECK:         [[BIG:%.*]] = alloca %struct.Big
// CHECK:         [[RECEIVER:%.*]] = load ptr, ptr
// CHECK:         [[ISNIL:%.*]] = icmp eq ptr [[RECEIVER]], null
// CHECK:         store %struct.Big zeroinitializer, ptr [[BIG]], align 4
// CHECK:         @objc_msg_lookup_sender
// CHECK:         call void %{{.*}}(ptr dead_on_unwind writable sret(%struct.Big) align 4 [[BIG]]
void test_loop_zeroing(id<P> p) {
  for (;;) {
    Big big = [p foo];
  }
}

// CHECK-LABEL: define{{.*}} void @test_zeroing_and_consume(
// CHECK:         [[BIG:%.*]] = alloca %struct.Big
// CHECK:         [[Q_RETAIN:%.*]] = call ptr @llvm.objc.retain(ptr
// CHECK:         [[ISNIL:%.*]] = icmp eq ptr
// CHECK:         call void @llvm.objc.release(ptr [[Q_RETAIN]])
// CHECK:         store %struct.Big zeroinitializer, ptr [[BIG]], align 4
// CHECK:         @objc_msg_lookup_sender
// CHECK:         call void %{{.*}}(ptr dead_on_unwind writable sret(%struct.Big) align 4 [[BIG]]
void test_zeroing_and_consume(id<P> p, id q) {
  for (;;) {
    Big big = [p fooConsuming:q];
  }
}

// CHECK-LABEL: define{{.*}} void @test_complex(
// CHECK:         [[RECEIVER:%.*]] = load ptr, ptr
// CHECK:         [[ISNIL:%.*]] = icmp eq ptr [[RECEIVER]], null
// CHECK:         @objc_msg_lookup_sender
// CHECK:         call <2 x float> %{{.*}}(ptr noundef %{{.*}}, ptr noundef @".objc_selector_complex_jf16\010:8"
// CHECK:         phi { float, float } [ %{{.*}}, %{{.*}} ], [ zeroinitializer, %{{.*}} ]
void test_complex(id<P> p) {
  _Complex float f = [p complex];
}

// CHECK: attributes #[[SUPER_LOOKUP_ATTR]] = { nounwind }
// CHECK: ![[FOO_MD]] = !{!"foo", !"", i1 false}
// CHECK: ![[SUPER_FOO_MD]] = !{!"foo", !"SuperClass", i1 false}
