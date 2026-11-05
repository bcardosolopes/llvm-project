// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Verify noundef is propagated to indirect call arguments,
// matching OGCG's behavior for function pointer calls.

typedef int (*fn_ptr)(int, int);

int call_indirect(fn_ptr f, int a, int b) {
  return f(a, b);
}

// CIR: cir.call %{{.*}}(%{{.*}}, %{{.*}}) {cir.call_arg_attrs = [{cir.noundef}, {cir.noundef}]}
// LLVM: call i32 %{{.*}}(i32 noundef %{{.*}}, i32 noundef %{{.*}})
