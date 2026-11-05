// RUN: %clang_cc1 -DSTRET -triple x86_64-pc-linux-gnu -fobjc-runtime=objfw -emit-llvm -fclangir -o - %s | FileCheck %s --check-prefix=OBJFW
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fobjc-runtime=gcc -emit-llvm -fclangir -o - %s | FileCheck %s --check-prefix=GCC

struct test {
  char test[1024];
};

@interface Test0
+ (struct test)test;
@end

void test0(void) {
  struct test t;
#if (defined(STRET) && defined(__OBJFW_RUNTIME_ABI__)) || \
    (!defined(STRET) && !defined(__OBJFW_RUNTIME_ABI__))
  t = [Test0 test];
#endif
  (void)t;
}

// OBJFW-LABEL: define{{.*}} void @test0()
// OBJFW-NOT: @objc_lookup_class
// OBJFW: [[LOOKUP:%.*]] = call ptr @objc_msg_lookup_stret(ptr @_OBJC_CLASS_Test0, ptr {{.*}}), !GNUObjCMessageSend ![[MSG:[0-9]+]]
// OBJFW-NEXT: call void [[LOOKUP]](ptr dead_on_unwind writable sret(%struct.test) {{.*}}, ptr noundef @_OBJC_CLASS_Test0, ptr noundef {{.*}}), !GNUObjCMessageSend ![[MSG]]
// OBJFW: ![[MSG]] = !{!"test", !"Test0", i1 true}

// GCC-LABEL: define{{.*}} void @test0()
// GCC-NOT: @objc_msg_lookup_stret
// GCC: [[LOOKUP:%.*]] = call ptr @objc_msg_lookup(ptr {{.*}})
// GCC-NEXT: call void [[LOOKUP]](ptr dead_on_unwind writable sret(%struct.test) {{.*}}, ptr {{.*}}, ptr noundef {{.*}})
