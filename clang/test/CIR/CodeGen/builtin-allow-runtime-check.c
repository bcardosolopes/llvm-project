// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-mlir=cir-abi -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=OGCG

_Bool test_allow_runtime_check(void) {
  return __builtin_allow_runtime_check("mycheck");
}

_Bool test_allow_runtime_check_twice(void) {
  return __builtin_allow_runtime_check("mycheck") |
         __builtin_allow_runtime_check("mycheck");
}

_Bool __cir_allow_runtime_check(void *p) {
  return p != 0;
}

_Bool test_allow_runtime_check_with_user_symbol(void) {
  return __builtin_allow_runtime_check("mycheck2");
}

// CIR-LABEL: cir.func {{.*}} @test_allow_runtime_check() -> (!cir.bool {cir.zeroext})
// CIR: %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!void>
// CIR: %[[ALLOW:.*]] = cir.call @__cir_allow_runtime_check(%[[NULL]]) {cir.call_allow_runtime_check_kind = "mycheck"} : (!cir.ptr<!void>) -> !cir.bool

// CIR-LABEL: cir.func {{.*}} @test_allow_runtime_check_twice() -> (!cir.bool {cir.zeroext})
// CIR: %[[NULL0:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!void>
// CIR: %[[ALLOW0:.*]] = cir.call @__cir_allow_runtime_check(%[[NULL0]]) {cir.call_allow_runtime_check_kind = "mycheck"} : (!cir.ptr<!void>) -> !cir.bool
// CIR: %[[NULL1:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!void>
// CIR: %[[ALLOW1:.*]] = cir.call @__cir_allow_runtime_check(%[[NULL1]]) {cir.call_allow_runtime_check_kind = "mycheck"} : (!cir.ptr<!void>) -> !cir.bool

// CIR-LABEL: cir.func {{.*}} @test_allow_runtime_check_with_user_symbol() -> (!cir.bool {cir.zeroext})
// CIR: cir.call @__cir_allow_runtime_check

// LLVM-DAG: define dso_local i1 @__cir_allow_runtime_check(
// LLVM-LABEL: @test_allow_runtime_check(
// LLVM: call i1 @llvm.allow.runtime.check(metadata{{( noundef)?}} !"mycheck")
// LLVM-LABEL: @test_allow_runtime_check_twice(
// LLVM: call i1 @llvm.allow.runtime.check(metadata{{( noundef)?}} !"mycheck")
// LLVM: call i1 @llvm.allow.runtime.check(metadata{{( noundef)?}} !"mycheck")
// LLVM-LABEL: @test_allow_runtime_check_with_user_symbol(
// LLVM: call i1 @llvm.allow.runtime.check(metadata{{( noundef)?}} !"mycheck2")
// LLVM-NOT: declare i1 @__cir_allow_runtime_check

// OGCG-LABEL: @test_allow_runtime_check(
// OGCG: call i1 @llvm.allow.runtime.check(metadata !"mycheck")
// OGCG-LABEL: @test_allow_runtime_check_twice(
// OGCG: call i1 @llvm.allow.runtime.check(metadata !"mycheck")
// OGCG: call i1 @llvm.allow.runtime.check(metadata !"mycheck")
// OGCG-LABEL: @__cir_allow_runtime_check(
// OGCG-LABEL: @test_allow_runtime_check_with_user_symbol(
// OGCG: call i1 @llvm.allow.runtime.check(metadata !"mycheck2")
