// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=OGCG

_Bool allow0(void) { return __builtin_allow_runtime_check("first"); }
_Bool allow1(void) { return __builtin_allow_runtime_check("second"); }

// CIR-LABEL: @allow0
// CIR: cir.call @__cir_allow_runtime_check{{.*}} {cir.call_allow_runtime_check_kind = "first"}
// CIR-LABEL: @allow1
// CIR: cir.call @__cir_allow_runtime_check{{.*}} {cir.call_allow_runtime_check_kind = "second"}

// LLVM-LABEL: @allow0
// LLVM: call i1 @llvm.allow.runtime.check(metadata{{( noundef)?}} !"first")
// LLVM-LABEL: @allow1
// LLVM: call i1 @llvm.allow.runtime.check(metadata{{( noundef)?}} !"second")

// OGCG-LABEL: @allow0
// OGCG: call i1 @llvm.allow.runtime.check(metadata !"first")
// OGCG-LABEL: @allow1
// OGCG: call i1 @llvm.allow.runtime.check(metadata !"second")
