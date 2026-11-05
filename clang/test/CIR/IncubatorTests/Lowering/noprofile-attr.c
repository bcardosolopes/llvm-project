// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that __attribute__((no_profile_instrument_function)) emits
// the noprofile LLVM function attribute, matching OGCG behavior.

void __attribute__((no_profile_instrument_function)) noprof_func(void) {}
// CHECK-LABEL: define {{.*}}@noprof_func
// CHECK-SAME: #[[NOPROF_ATTR:[0-9]+]]

void normal_func(void) {}
// CHECK-LABEL: define {{.*}}@normal_func
// CHECK-SAME: #[[NORMAL_ATTR:[0-9]+]]

// CHECK: attributes #[[NOPROF_ATTR]] = {{{.*}}noprofile{{.*}}}
// CHECK-NOT: attributes #[[NORMAL_ATTR]] = {{{.*}}noprofile{{.*}}}
