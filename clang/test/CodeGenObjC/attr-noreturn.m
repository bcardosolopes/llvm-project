// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -o - %s | FileCheck %s -check-prefix=CHECK -check-prefix=CHECK-MRC
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -o - %s | FileCheck %s -check-prefix=CHECK -check-prefix=CHECK-ARC
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fclangir -o - %s | FileCheck %s -check-prefix=CHECK-CIR-ARC %}

__attribute__((objc_root_class))
@interface Root
- (instancetype) init;
@end

@interface Base : Root
@end

@interface Middle : Base
+ (void) abort __attribute__((noreturn));
- (void) fail __attribute__((noreturn));
@end
  
@interface Derived : Middle
@end

@interface DirectStrong : Root
+ (void) abort __attribute__((noreturn, objc_direct));
@end

@implementation DirectStrong
+ (void) abort {
  while (1) {
  }
}
@end

__attribute__((weak_import))
@interface DirectWeakBase : Root
@end

@interface DirectWeak : DirectWeakBase
+ (void) abort __attribute__((noreturn, objc_direct));
@end

@implementation DirectWeak
+ (void) abort {
  while (1) {
  }
}
@end

// An arbitrary instance pointer may be null.
void testInstanceMethod(Derived *x) {
  [x fail];
}
// CHECK-LABEL: @testInstanceMethod
// CHECK: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}){{$}}

// A direct call of a class method will normally never have a null receiver.
void testClassMethod(void) {
  [Derived abort];
}
// CHECK-LABEL: @testClassMethod
// CHECK: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}) [[NORETURN:#[0-9]+]]
// CHECK-CIR-ARC-LABEL: @testClassMethod
// CHECK-CIR-ARC: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}) [[NORETURN_CIR:#[0-9]+]]

void testStrongDirectClassMethod(void) {
  [DirectStrong abort];
}
// CHECK-LABEL: @testStrongDirectClassMethod
// CHECK: call void @"\01+[DirectStrong abort]"(ptr {{.*}}) [[NORETURN]]
// CHECK-NEXT: unreachable
// CHECK-CIR-ARC-LABEL: @testStrongDirectClassMethod
// CHECK-CIR-ARC: call void @"\01+[DirectStrong abort]"(ptr {{.*}}) [[NORETURN_CIR]]
// CHECK-CIR-ARC-NEXT: unreachable

void testWeakDirectClassMethod(void) {
  [DirectWeak abort];
}
// CHECK-LABEL: @testWeakDirectClassMethod
// CHECK: call void @"\01+[DirectWeak abort]"(ptr {{.*}}){{$}}
// CHECK-NEXT: unreachable
// CHECK-CIR-ARC-LABEL: @testWeakDirectClassMethod
// CHECK-CIR-ARC: call void @"\01+[DirectWeak abort]"(ptr {{.*}}){{$}}
// CHECK-CIR-ARC-NEXT: unreachable

__attribute__((weak_import))
@interface WeakMiddle : Base
@end
  
@interface WeakDerived : WeakMiddle
+ (void) abort __attribute__((noreturn));
@end

// The class pointer of a weakly-imported class may be null.
void testWeakImport(void) {
  [WeakDerived abort];
}
// CHECK-LABEL: @testWeakImport
// CHECK: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}){{$}}
// CHECK-CIR-ARC-LABEL: @testWeakImport
// CHECK-CIR-ARC: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}){{$}}

@interface Derived (MyMethods)
@end

@implementation Derived (MyMethods)

// In general, self can be reassigned, so we can't make stronger assumptions.
// But ARC makes self const in an ordinary method.
// TODO: do the analysis to take advantage of the dominant case where
// self is not reassigned.
- (void) testSelfInstanceMethod {
  [self fail];
}
// CHECK-LABEL: [Derived(MyMethods) testSelfInstanceMethod]
// CHECK-MRC: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}){{$}}
// CHECK-ARC: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}) [[NORETURN]]
// CHECK-CIR-ARC-LABEL: [Derived(MyMethods) testSelfInstanceMethod]
// CHECK-CIR-ARC: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}) [[NORETURN_CIR]]

// The ARC rule doesn't apply in -init methods.
- (id) initWhileTestingSelfInstanceMethod {
  self = [super init];
  [self fail];
  return self;
}
// CHECK-LABEL: [Derived(MyMethods) initWhileTestingSelfInstanceMethod]
// CHECK: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}){{$}}
// CHECK-CIR-ARC-LABEL: [Derived(MyMethods) initWhileTestingSelfInstanceMethod]
// CHECK-CIR-ARC: call ptr @objc_msgSendSuper2(ptr {{.*}}, ptr {{.*}})
// CHECK-CIR-ARC: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}){{$}}

// Same thing applies to class methods.
+ (void) testSelfClassMethod {
  [self abort];
}
// CHECK-LABEL: [Derived(MyMethods) testSelfClassMethod]
// CHECK-MRC: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}){{$}}
// CHECK-ARC: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}) [[NORETURN]]
// CHECK-CIR-ARC-LABEL: [Derived(MyMethods) testSelfClassMethod]
// CHECK-CIR-ARC: call void @objc_msgSend(ptr {{.*}}, ptr {{.*}}) [[NORETURN_CIR]]

// Super invocations may never be used with a null pointer; this is a
// constraint on user code when it isn't enforced by the ARC const-self
// rule.
- (void) testSuperInstanceMethod {
  [super fail];
}
// CHECK-LABEL: [Derived(MyMethods) testSuperInstanceMethod]
// CHECK: call void @objc_msgSendSuper2(ptr {{.*}}, ptr {{.*}}) [[NORETURN]]
// CHECK-CIR-ARC-LABEL: [Derived(MyMethods) testSuperInstanceMethod]
// CHECK-CIR-ARC: call void @objc_msgSendSuper2(ptr {{.*}}, ptr {{.*}}) [[NORETURN_CIR]]

+ (void) testSuperClassMethod {
  [super abort];
}
// CHECK-LABEL: [Derived(MyMethods) testSuperClassMethod]
// CHECK: call void @objc_msgSendSuper2(ptr {{.*}}, ptr {{.*}}) [[NORETURN]]
// CHECK-CIR-ARC-LABEL: [Derived(MyMethods) testSuperClassMethod]
// CHECK-CIR-ARC: call void @objc_msgSendSuper2(ptr {{.*}}, ptr {{.*}}) [[NORETURN_CIR]]
@end

// CHECK: attributes [[NORETURN]] = { noreturn }
// CHECK-CIR-ARC: attributes [[NORETURN_CIR]] = { noreturn }
  
