// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -std=c++20 -emit-llvm -O1 -disable-llvm-passes -fclangir %s -o - | FileCheck %s

// Regression: deferred AArch64 target_clones placeholders must
// preserve method parameter attrs strongly enough for direct-call lowering to
// re-materialize the OGCG-equivalent call-site nonnull/align/dereferenceable
// attrs on the unsuffixed FMV declaration.

int __attribute__((target_clones("fp16"))) foo_ovl() { return 2; }

int run_foo_ovl() { return foo_ovl(); }

template <typename T1, typename T2> struct MyClass {
  int __attribute__((target_clones("frintts", "ssbs+sme-f64f64", "default")))
  foo_tml() {
    return 1;
  }
};

template <typename T> struct MyClass<int, T> {
  int __attribute__((target_clones("frintts", "ssbs+sme-f64f64"))) foo_tml() {
    return 2;
  }
};

void run_foo_tml() {
  MyClass<int, short> mc;
  mc.foo_tml();
}

// CHECK-DAG: declare noundef i32 @_Z7foo_ovlv() #[[FREE_ATTRS:[0-9]+]]
// CHECK-LABEL: define{{.*}} noundef i32 @_Z11run_foo_ovlv()
// CHECK: call noundef i32 @_Z7foo_ovlv()
// CHECK: declare noundef i32 @_ZN7MyClassIisE7foo_tmlEv(ptr noundef nonnull align 1 dereferenceable(1)) #[[METHOD_ATTRS:[0-9]+]]
// CHECK-LABEL: define{{.*}} void @_Z11run_foo_tmlv()
// CHECK: call noundef i32 @_ZN7MyClassIisE7foo_tmlEv(ptr noundef nonnull align 1 dereferenceable(1) {{.*}})
// CHECK: attributes #[[FREE_ATTRS]] = {
// CHECK-SAME: "fmv-features"="fp16"
// CHECK-SAME: "target-features"="+fp-armv8,+fullfp16"
// CHECK: attributes #[[METHOD_ATTRS]] = {
// CHECK-SAME: "fmv-features"="frintts"
// CHECK-SAME: "target-features"="+fp-armv8,+fptoint"
