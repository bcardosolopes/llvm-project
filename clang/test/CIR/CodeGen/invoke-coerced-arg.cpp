// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm -fexceptions -fcxx-exceptions %s -o - | FileCheck %s

// Verify that CIR correctly handles invoke instructions where the call-site
// argument types differ from the declared function parameter types due to
// ABI coercion (e.g., a small struct coerced to i64).
//
// Without the fix in LLVMToLLVMIRTranslation.cpp, cases where the declared
// function type and call-site type differ (same param count, different types)
// would trigger an assertion in LLVM IR InvokeInst:
//   "Invoking a function with a bad signature!"

struct SmallStruct {
  void *p;
};

int may_throw(SmallStruct s);

// CHECK-LABEL: define{{.*}} @_Z23test_invoke_coerced_arg11SmallStruct
int test_invoke_coerced_arg(SmallStruct s) {
  // CHECK: invoke{{.*}} @_Z9may_throw11SmallStruct
  // CHECK-NEXT: to label %{{.*}} unwind label %{{.*}}
  try {
    return may_throw(s);
  } catch (...) {
    return -1;
  }
}

// CHECK: landingpad
