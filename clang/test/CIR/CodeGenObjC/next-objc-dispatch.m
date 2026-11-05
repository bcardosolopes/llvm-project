// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fobjc-runtime=macosx-fragile-10.5 -fclangir -emit-llvm -o - %s -fobjc-dispatch-method=legacy | FileCheck %s --check-prefix=FRAGILE-LEGACY
// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fclangir -emit-llvm -o - %s -fobjc-dispatch-method=legacy | FileCheck %s --check-prefix=NONFRAGILE-LEGACY
// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fclangir -emit-llvm -o - %s -fobjc-dispatch-method=non-legacy | FileCheck %s --check-prefix=NONFRAGILE-NONLEGACY
// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fclangir -emit-llvm -o - %s -fobjc-dispatch-method=mixed | FileCheck %s --check-prefix=NONFRAGILE-MIXED

@interface NSObject
+ (id)alloc;
- (id)init;
@end

typedef struct {
  long a, b, c;
} Big;

@interface I0 : NSObject
- (void)im0;
@end

@implementation I0
+ (id)alloc {
  return [super alloc];
}

- (id)init {
  [super init];
  return self;
}

- (void)im0 {}
@end

void f0(I0 *a) {
  [I0 alloc];
  [a im0];
}

@interface ReturnTest : NSObject
- (Big)count;
- (long double)length;
@end

@implementation ReturnTest
- (Big)count {
  return (Big){1, 2, 3};
}

- (long double)length {
  return 1;
}
@end

Big countCaller(ReturnTest *a) {
  return [a count];
}

long double lengthCaller(ReturnTest *a) {
  return [a length];
}

// FRAGILE-LEGACY-LABEL: define internal ptr @"\01+[I0 alloc]"
// FRAGILE-LEGACY: call ptr @objc_msgSendSuper(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// FRAGILE-LEGACY: declare ptr @objc_msgSendSuper{{[(]}}
// FRAGILE-LEGACY-LABEL: define internal ptr @"\01-[I0 init]"
// FRAGILE-LEGACY: call ptr @objc_msgSendSuper(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// FRAGILE-LEGACY: declare ptr @objc_msgSend{{[(]}}
// FRAGILE-LEGACY-LABEL: define void @f0(
// FRAGILE-LEGACY: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// FRAGILE-LEGACY: call void @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}})

// NONFRAGILE-LEGACY-LABEL: define internal ptr @"\01+[I0 alloc]"
// NONFRAGILE-LEGACY: call ptr @objc_msgSendSuper2(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// NONFRAGILE-LEGACY: declare ptr @objc_msgSendSuper2{{[(]}}
// NONFRAGILE-LEGACY-LABEL: define internal ptr @"\01-[I0 init]"
// NONFRAGILE-LEGACY: call ptr @objc_msgSendSuper2(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// NONFRAGILE-LEGACY: declare ptr @objc_msgSend{{[(]}}
// NONFRAGILE-LEGACY-LABEL: define void @f0(
// NONFRAGILE-LEGACY: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// NONFRAGILE-LEGACY: call void @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}})

// NONFRAGILE-NONLEGACY-DAG: @_objc_msgSendSuper2_fixup_alloc = weak hidden global { ptr, ptr } { ptr @objc_msgSendSuper2_fixup, ptr @OBJC_METH_VAR_NAME_{{[0-9.]*}} }, section "__DATA,__objc_msgrefs,coalesced", align 16
// NONFRAGILE-NONLEGACY-DAG: @_objc_msgSendSuper2_fixup_init = weak hidden global { ptr, ptr } { ptr @objc_msgSendSuper2_fixup, ptr @OBJC_METH_VAR_NAME_{{[0-9.]*}} }, section "__DATA,__objc_msgrefs,coalesced", align 16
// NONFRAGILE-NONLEGACY-DAG: @_objc_msgSend_fixup_alloc = weak hidden global { ptr, ptr } { ptr @objc_msgSend_fixup, ptr @OBJC_METH_VAR_NAME_{{[0-9.]*}} }, section "__DATA,__objc_msgrefs,coalesced", align 16
// NONFRAGILE-NONLEGACY-DAG: @_objc_msgSend_fixup_im0 = weak hidden global { ptr, ptr } { ptr @objc_msgSend_fixup, ptr @OBJC_METH_VAR_NAME_{{[0-9.]*}} }, section "__DATA,__objc_msgrefs,coalesced", align 16
// NONFRAGILE-NONLEGACY-DAG: @_objc_msgSend_stret_fixup_count = weak hidden global { ptr, ptr } { ptr @objc_msgSend_stret_fixup, ptr @OBJC_METH_VAR_NAME_{{[0-9.]*}} }, section "__DATA,__objc_msgrefs,coalesced", align 16
// NONFRAGILE-NONLEGACY-DAG: @_objc_msgSend_fpret_fixup_length = weak hidden global { ptr, ptr } { ptr @objc_msgSend_fpret_fixup, ptr @OBJC_METH_VAR_NAME_{{[0-9.]*}} }, section "__DATA,__objc_msgrefs,coalesced", align 16
// NONFRAGILE-NONLEGACY-LABEL: define internal ptr @"\01+[I0 alloc]"
// NONFRAGILE-NONLEGACY: load ptr, ptr @_objc_msgSendSuper2_fixup_alloc, align 8
// NONFRAGILE-NONLEGACY: call ptr %{{.*}}(ptr noundef %{{.*}}, ptr noundef @_objc_msgSendSuper2_fixup_alloc)
// NONFRAGILE-NONLEGACY: declare ptr @objc_msgSendSuper2_fixup{{[(]}}
// NONFRAGILE-NONLEGACY-LABEL: define internal ptr @"\01-[I0 init]"
// NONFRAGILE-NONLEGACY: load ptr, ptr @_objc_msgSendSuper2_fixup_init, align 8
// NONFRAGILE-NONLEGACY: call ptr %{{.*}}(ptr noundef %{{.*}}, ptr noundef @_objc_msgSendSuper2_fixup_init)
// NONFRAGILE-NONLEGACY: declare ptr @objc_msgSend_fixup{{[(]}}
// NONFRAGILE-NONLEGACY-LABEL: define void @f0(
// NONFRAGILE-NONLEGACY: load ptr, ptr @_objc_msgSend_fixup_alloc, align 8
// NONFRAGILE-NONLEGACY: call ptr %{{.*}}(ptr noundef %{{.*}}, ptr noundef @_objc_msgSend_fixup_alloc)
// NONFRAGILE-NONLEGACY: load ptr, ptr @_objc_msgSend_fixup_im0, align 8
// NONFRAGILE-NONLEGACY: call void %{{.*}}(ptr noundef %{{.*}}, ptr noundef @_objc_msgSend_fixup_im0)
// NONFRAGILE-NONLEGACY: declare ptr @objc_msgSend_stret_fixup{{[(]}}
// NONFRAGILE-NONLEGACY-LABEL: define void @countCaller(
// NONFRAGILE-NONLEGACY: load ptr, ptr @_objc_msgSend_stret_fixup_count, align 8
// NONFRAGILE-NONLEGACY: call void %{{.*}}(ptr{{.*}}sret(%struct.Big) align 8 %{{.*}}, ptr noundef %{{.*}}, ptr noundef @_objc_msgSend_stret_fixup_count)
// NONFRAGILE-NONLEGACY: declare ptr @objc_msgSend_fpret_fixup{{[(]}}
// NONFRAGILE-NONLEGACY-LABEL: define x86_fp80 @lengthCaller(
// NONFRAGILE-NONLEGACY: load ptr, ptr @_objc_msgSend_fpret_fixup_length, align 8
// NONFRAGILE-NONLEGACY: call x86_fp80 %{{.*}}(ptr noundef %{{.*}}, ptr noundef @_objc_msgSend_fpret_fixup_length)

// NONFRAGILE-MIXED-LABEL: define internal ptr @"\01+[I0 alloc]"
// NONFRAGILE-MIXED: load ptr, ptr @_objc_msgSendSuper2_fixup_alloc, align 8
// NONFRAGILE-MIXED: call ptr %{{.*}}(ptr noundef %{{.*}}, ptr noundef @_objc_msgSendSuper2_fixup_alloc)
// NONFRAGILE-MIXED: declare ptr @objc_msgSendSuper2_fixup{{[(]}}
// NONFRAGILE-MIXED-LABEL: define internal ptr @"\01-[I0 init]"
// NONFRAGILE-MIXED: load ptr, ptr @OBJC_SELECTOR_REFERENCES_{{[0-9.]*}}, align 8, !invariant.load ![[EMPTY:[0-9]+]]
// NONFRAGILE-MIXED: call ptr @objc_msgSendSuper2(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// NONFRAGILE-MIXED: declare ptr @objc_msgSendSuper2{{[(]}}
// NONFRAGILE-MIXED: declare ptr @objc_msgSend_fixup{{[(]}}
// NONFRAGILE-MIXED: declare ptr @objc_msgSend{{[(]}}
// NONFRAGILE-MIXED-LABEL: define void @f0(
// NONFRAGILE-MIXED: load ptr, ptr @_objc_msgSend_fixup_alloc, align 8
// NONFRAGILE-MIXED: call ptr %{{.*}}(ptr noundef %{{.*}}, ptr noundef @_objc_msgSend_fixup_alloc)
// NONFRAGILE-MIXED: load ptr, ptr @OBJC_SELECTOR_REFERENCES_{{[0-9.]*}}, align 8, !invariant.load ![[EMPTY]]
// NONFRAGILE-MIXED: call void @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}})
