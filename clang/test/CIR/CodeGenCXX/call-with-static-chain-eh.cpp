// RUN: %clang_cc1 -triple x86_64-unknown-unknown -std=c++17 -fcxx-exceptions -fexceptions -fclangir -emit-mlir=cir-abi -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -std=c++17 -fcxx-exceptions -fexceptions -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM

extern "C" void may_throw();

void test() {
  try {
    __builtin_call_with_static_chain(may_throw(), may_throw);
  } catch (...) {
  }
}

// CIR-LABEL: cir.func {{.*}}@_Z4testv()
// CIR: cir.try {
// CIR: cir.call exception @may_throw(%{{.*}}) {cir.call_arg_attrs = [{cir.nest, cir.noundef}]} : (!cir.ptr<!cir.func<()>>) -> ()

// LLVM-LABEL: define dso_local void @_Z4testv() #0 personality ptr @__gxx_personality_v0 {
// LLVM: invoke void @may_throw(ptr nest noundef @may_throw)
// LLVM: landingpad { ptr, i32 }
