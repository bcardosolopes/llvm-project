// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fclangir -emit-llvm -o - %s -fobjc-dispatch-method=non-legacy | FileCheck %s --check-prefix=NONLEGACY
// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fclangir -emit-llvm -o - %s -fobjc-dispatch-method=mixed | FileCheck %s --check-prefix=MIXED

__attribute__((objc_root_class))
@interface NSObject
+ (id)self;
@end

@interface Root : NSObject
+ (int)cls __attribute__((objc_direct));
@end

@implementation Root
+ (int)cls {
  return 42;
}
@end

// NONLEGACY-DAG: @_objc_msgSend_fixup_self = weak hidden global { ptr, ptr } { ptr @objc_msgSend_fixup, ptr @OBJC_METH_VAR_NAME_{{[0-9.]*}} }, section "__DATA,__objc_msgrefs,coalesced", align 16
// NONLEGACY-LABEL: define hidden i32 @"\01+[Root cls]"(
// NONLEGACY-NOT: @OBJC_SELECTOR_REFERENCES_
// NONLEGACY: [[MSGSEND:%.*]] = load ptr, ptr @_objc_msgSend_fixup_self, align 8
// NONLEGACY: call ptr [[MSGSEND]](ptr noundef %{{.*}}, ptr noundef @_objc_msgSend_fixup_self)

// MIXED-DAG: @_objc_msgSend_fixup_self = weak hidden global { ptr, ptr } { ptr @objc_msgSend_fixup, ptr @OBJC_METH_VAR_NAME_{{[0-9.]*}} }, section "__DATA,__objc_msgrefs,coalesced", align 16
// MIXED-LABEL: define hidden i32 @"\01+[Root cls]"(
// MIXED-NOT: @OBJC_SELECTOR_REFERENCES_
// MIXED: [[MSGSEND:%.*]] = load ptr, ptr @_objc_msgSend_fixup_self, align 8
// MIXED: call ptr [[MSGSEND]](ptr noundef %{{.*}}, ptr noundef @_objc_msgSend_fixup_self)
