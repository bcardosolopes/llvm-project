// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm \
// RUN:   -mbranch-target-enforce -msign-return-address=all \
// RUN:   -msign-return-address-key=a_key -mguarded-control-stack \
// RUN:   %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// RUN: %clang_cc1 -triple aarch64-linux-gnu -emit-llvm \
// RUN:   -mbranch-target-enforce -msign-return-address=all \
// RUN:   -msign-return-address-key=a_key -mguarded-control-stack \
// RUN:   %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify CIR emits branch protection function attributes matching OGCG.

void foo(void) {}

// CHECK: define {{.*}}void @foo() #[[ATTR:[0-9]+]]
// CHECK: attributes #[[ATTR]] = {
// CHECK-SAME: "branch-target-enforcement"
// CHECK-SAME: "guarded-control-stack"
// CHECK-SAME: "sign-return-address"="all"
// CHECK-SAME: "sign-return-address-key"="a_key"

// OGCG: define {{.*}}void @foo() #[[ATTR:[0-9]+]]
// OGCG: attributes #[[ATTR]] = {
// OGCG-SAME: "branch-target-enforcement"
// OGCG-SAME: "guarded-control-stack"
// OGCG-SAME: "sign-return-address"="all"
// OGCG-SAME: "sign-return-address-key"="a_key"
