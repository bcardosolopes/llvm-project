// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-cir -fclangir -o - %s | FileCheck %s

__attribute__((objc_root_class))
@interface Root
- (id)init;
@end

@interface Base : Root
- (void)instMethod;
+ (void)classMethod;
@property(assign) int x;
@end

@interface Derived : Base
@end

typedef struct {
  long a, b, c;
} Big;

@interface Base (StructReturn)
- (Big)big;
@end

Big direct_big(Base *base) {
  return [base big];
}

Big nil_big(void) {
  return [(Base *)0 big];
}

@implementation Derived
- (id)init {
  return [super init];
}

- (void)instMethod {
  [super instMethod];
}

+ (void)classMethod {
  [super classMethod];
}

- (int)x {
  return super.x + 1;
}

- (Big)big {
  return [super big];
}
@end

// CHECK: cir.func no_inline optnone @direct_big
// CHECK: cir.get_global @objc_msgSend_stret
// CHECK: cir.if
// CHECK: cir.call
// CHECK: cir.func no_inline optnone @nil_big
// CHECK: cir.const #cir.zero : !rec_Big
// CHECK: cir.store align(8)
// CHECK: cir.func no_inline optnone internal private{{( dso_local)?}} @"\01-[Derived init]"
// CHECK: cir.get_global @objc_msgSendSuper2
// CHECK: cir.func no_inline optnone internal private{{( dso_local)?}} @"\01-[Derived instMethod]"
// CHECK: cir.get_global @objc_msgSendSuper2
// CHECK: cir.func no_inline optnone internal private{{( dso_local)?}} @"\01+[Derived classMethod]"
// CHECK: @OBJC_CLASSLIST_SUP_REFS_$_.1
// CHECK: cir.get_global @objc_msgSendSuper2
// CHECK: cir.func no_inline optnone internal private{{( dso_local)?}} @"\01-[Derived x]"
// CHECK: cir.get_global @objc_msgSendSuper2
// CHECK: cir.func no_inline optnone internal private{{( dso_local)?}} @"\01-[Derived big]"
// CHECK: cir.get_global @objc_msgSendSuper2_stret
