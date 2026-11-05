// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fobjc-runtime=gnustep-2.2 -fobjc-dispatch-method=non-legacy -emit-llvm -fclangir -o - %s | FileCheck %s --check-prefixes=COMMON,X86
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fobjc-runtime=gnustep-2.2 -fobjc-dispatch-method=non-legacy -emit-llvm -fclangir -o - %s | FileCheck %s --check-prefixes=COMMON,AARCH64

typedef struct {
  int x[12];
} Big;

typedef struct {
  double x[4];
} HFA;

__attribute__((objc_root_class))
@interface Root
@end

@implementation Root
@end

@interface SuperClass : Root
- (Big)bigFoo;
- (HFA)hfaFoo;
@end

@interface TestClass : SuperClass
+ (id)classFoo;
- (id)instFoo;
- (Big)bigFoo;
- (long double)longDoubleFoo;
- (id)testInstance;
+ (id)testClass;
- (Big)testBig:(SuperClass *)p;
- (HFA)testHFA:(SuperClass *)p;
- (long double)testLongDouble:(TestClass *)p;
- (Big)testSuperBig;
@end

@implementation TestClass

// COMMON-LABEL: define{{.*}} ptr @_i_TestClass__testInstance
// COMMON-SAME: #[[METHOD_ATTRS:[0-9]+]]
// COMMON-NOT: @objc_msg_lookup_sender
// COMMON: call ptr @objc_msgSend({{.*}}), !GNUObjCMessageSend ![[INST_MD:[0-9]+]]
- (id)testInstance {
  return [self instFoo];
}

// COMMON-LABEL: define{{.*}} ptr @_c_TestClass__testClass
// COMMON-NOT: @objc_msg_lookup_sender
// COMMON-NOT: @objc_lookup_class
// COMMON: load ptr, ptr @._OBJC_REF_CLASS_TestClass, align 8
// COMMON: call ptr @objc_msgSend({{.*}}), !GNUObjCMessageSend ![[CLASS_MD:[0-9]+]]
+ (id)testClass {
  return [TestClass classFoo];
}

// COMMON-LABEL: define{{.*}} @_i_TestClass__testBig_
// COMMON: call void @objc_msgSend_stret({{.*}}), !GNUObjCMessageSend ![[BIG_MD:[0-9]+]]
- (Big)testBig:(SuperClass *)p {
  return [p bigFoo];
}

// AARCH64-LABEL: define{{.*}} @_i_TestClass__testHFA_
// AARCH64-NOT: @objc_msgSend_stret
// AARCH64: call {{.*}} @objc_msgSend({{.*}}), !GNUObjCMessageSend ![[HFA_MD:[0-9]+]]
- (HFA)testHFA:(SuperClass *)p {
  return [p hfaFoo];
}

// X86-LABEL: define{{.*}} x86_fp80 @_i_TestClass__testLongDouble_
// X86: call x86_fp80 @objc_msgSend_fpret({{.*}}), !GNUObjCMessageSend ![[FPRET_MD:[0-9]+]]
- (long double)testLongDouble:(TestClass *)p {
  return [p longDoubleFoo];
}

// COMMON-LABEL: define{{.*}} @_i_TestClass__testSuperBig
// COMMON-NOT: call void @objc_msgSend_stret(
// COMMON: call ptr @objc_msg_lookup_super(
- (Big)testSuperBig {
  return [super bigFoo];
}

@end

// COMMON: attributes #[[METHOD_ATTRS]] = { noinline nounwind optnone
// COMMON: ![[INST_MD]] = !{!"instFoo", !"", i1 false}
// COMMON: ![[CLASS_MD]] = !{!"classFoo", !"TestClass", i1 true}
// COMMON: ![[BIG_MD]] = !{!"bigFoo", !"", i1 false}
// AARCH64: ![[HFA_MD]] = !{!"hfaFoo", !"", i1 false}
// X86: ![[FPRET_MD]] = !{!"longDoubleFoo", !"", i1 false}
