// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -o - %s | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CIR %}

static int gint;
struct SetSection { 
      int & at(int __n) { return gint; }
      const int& at(int __n) const { return gint; }
};

static SetSection gSetSection;

@interface SetShow
- (SetSection&)sections;
@end

@implementation SetShow
- (SetSection&) sections {
//  [self sections].at(100);
    self.sections.at(100);
   return gSetSection;
}
@end

// CHECK: [[SELF:%.*]] = alloca ptr, align
// CHECK: [[T0:%.*]] = load {{.*}}, ptr [[SELF]], align
// CHECK: [[T1:%.*]] = load {{.*}}, ptr @OBJC_SELECTOR_REFERENCES_
// CHECK: [[C:%.*]] = call noundef nonnull align {{[0-9]+}} dereferenceable({{[0-9]+}}) ptr @objc_msgSend
// CHECK: call noundef nonnull align {{[0-9]+}} dereferenceable({{[0-9]+}}) ptr @_ZN10SetSection2atEi(ptr {{[^,]*}} [[C]]
// CIR-LABEL: define internal noundef nonnull align {{[0-9]+}} dereferenceable({{[0-9]+}}) ptr @"\01-[SetShow sections]"(
// CIR: load ptr, ptr @OBJC_SELECTOR_REFERENCES_
// CIR: call noundef nonnull align {{[0-9]+}} dereferenceable({{[0-9]+}}) ptr @objc_msgSend
// CIR: call noundef nonnull align {{[0-9]+}} dereferenceable({{[0-9]+}}) ptr @_ZN10SetSection2atEi(ptr {{[^,]*}} %{{.*}}, i32 noundef 100)
