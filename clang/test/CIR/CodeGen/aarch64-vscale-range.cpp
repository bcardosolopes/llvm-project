// RUN: %clang_cc1 -triple aarch64-linux-gnu -target-feature +sve -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -target-feature +sve -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -target-feature +sve -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

void f() {}

// CIR: vscale_range = array<i32: 1, 16>

// LLVM: define {{.*}} void @_Z1fv() #[[ATTR:[0-9]+]]
// LLVM: attributes #[[ATTR]] = {
// LLVM-SAME: vscale_range(1,16)

// OGCG: define {{.*}} void @_Z1fv() #[[ATTR:[0-9]+]]
// OGCG: attributes #[[ATTR]] = {
// OGCG-SAME: vscale_range(1,16)
