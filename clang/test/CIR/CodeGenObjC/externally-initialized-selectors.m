// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s --check-prefix=FRAGILE
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-10.14 -fclangir -emit-llvm -x objective-c %s -o - | FileCheck %s --check-prefix=NONFRAGILE

// NONFRAGILE: @OBJC_SELECTOR_REFERENCES_ = internal externally_initialized global ptr @OBJC_METH_VAR_NAME_, section "__DATA,__objc_selrefs,literal_pointers,no_dead_strip", align 8
// FRAGILE: @OBJC_SELECTOR_REFERENCES_ = private externally_initialized global ptr @OBJC_METH_VAR_NAME_, section "__OBJC,__message_refs,literal_pointers,no_dead_strip", align 8

@interface I
- (void)doSomething;
@end

void test(I *x) {
  // FRAGILE-LABEL: define void @test(
  // FRAGILE: %[[SEL:.*]] = load ptr, ptr @OBJC_SELECTOR_REFERENCES_, align 8
  // FRAGILE: call void @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %[[SEL]])

  // NONFRAGILE-LABEL: define void @test(
  // NONFRAGILE: %[[SEL:.*]] = load ptr, ptr @OBJC_SELECTOR_REFERENCES_, align 8, !invariant.load ![[INV:[0-9]+]]
  // NONFRAGILE: call void @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %[[SEL]])
  [x doSomething];
}
