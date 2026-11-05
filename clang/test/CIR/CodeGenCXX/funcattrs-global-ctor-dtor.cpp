// RUN: %clang_cc1 %s -triple x86_64-apple-darwin -stack-protector 2 -fclangir -emit-llvm -o - | FileCheck %s
// RUN: %clang_cc1 %s -triple x86_64-apple-darwin -stack-protector 2 -emit-llvm -o - | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 %s -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -fclangir -emit-llvm -o - | FileCheck %s --check-prefix=LINUX-O1
// RUN: %clang_cc1 %s -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -emit-llvm -o - | FileCheck %s --check-prefix=LINUX-O1-OGCG
// RUN: %clang_cc1 %s -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -fno-inline-functions -fclangir -emit-llvm -o - | FileCheck %s --check-prefix=LINUX-NOINLINE
// RUN: %clang_cc1 %s -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -fno-inline-functions -emit-llvm -o - | FileCheck %s --check-prefix=LINUX-NOINLINE-OGCG

// Test: compiler-generated global init/cleanup functions must
// have the full set of default function attributes (nounwind, sspstrong,
// target-features, etc.), not just noinline.

class A {
 public:
  virtual ~A() {}
};

A g;

// CHECK: define internal void @__cxx_global_var_init() [[ATTR0:#[0-9]+]]
// CHECK: define internal void @_GLOBAL__sub_I_{{.*}}() [[ATTR0]]
// CHECK: attributes [[ATTR0]] = {{{.*}} noinline nounwind sspstrong {{.*}}}

// OGCG: define internal void @__cxx_global_var_init() [[ATTR0:#[0-9]+]]
// OGCG: define internal void @_GLOBAL__sub_I_{{.*}}() [[ATTR0]]
// OGCG: attributes [[ATTR0]] = {{{.*}} noinline nounwind sspstrong {{.*}}}

// LINUX-O1: define internal void @__cxx_global_var_init() [[ATTR0:#[0-9]+]] section ".text.startup"
// LINUX-O1: define internal void @_GLOBAL__sub_I_{{.*}}() [[ATTR0]] section ".text.startup"
// LINUX-O1: attributes [[ATTR0]] = { nounwind
// LINUX-O1-NOT: noinline

// LINUX-O1-OGCG: define internal void @__cxx_global_var_init() [[ATTR0:#[0-9]+]] section ".text.startup"
// LINUX-O1-OGCG: define internal void @_GLOBAL__sub_I_{{.*}}() [[ATTR0]] section ".text.startup"
// LINUX-O1-OGCG: attributes [[ATTR0]] = { nounwind
// LINUX-O1-OGCG-NOT: noinline

// LINUX-NOINLINE: define internal void @__cxx_global_var_init() [[ATTR0:#[0-9]+]] section ".text.startup"
// LINUX-NOINLINE: define internal void @_GLOBAL__sub_I_{{.*}}() [[ATTR0]] section ".text.startup"
// LINUX-NOINLINE: attributes [[ATTR0]] = { noinline nounwind

// LINUX-NOINLINE-OGCG: define internal void @__cxx_global_var_init() [[ATTR0:#[0-9]+]] section ".text.startup"
// LINUX-NOINLINE-OGCG: define internal void @_GLOBAL__sub_I_{{.*}}() [[ATTR0]] section ".text.startup"
// LINUX-NOINLINE-OGCG: attributes [[ATTR0]] = { noinline nounwind
