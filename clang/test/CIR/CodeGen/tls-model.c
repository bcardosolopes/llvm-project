// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR %s < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll

__thread int a;
__thread int g __attribute__((tls_model("global-dynamic")));
__thread int h __attribute__((tls_model("local-dynamic")));
__thread int i __attribute__((tls_model("initial-exec")));
__thread int j __attribute__((tls_model("local-exec")));

int f(void) {
  __thread static int s __attribute__((tls_model("initial-exec")));
  return s++;
}

// CIR: cir.global  external tls_dyn  @a
// CIR: cir.global  external tls_dyn  @g
// CIR: cir.global  external tls_local_dyn  @h
// CIR: cir.global  external tls_init_exec  @i
// CIR: cir.global  external tls_local_exec  @j
// CIR: cir.global{{.*}}tls_init_exec{{.*}}@f.s

// LLVM: @a = thread_local global i32 0
// LLVM: @g = thread_local global i32 0
// LLVM: @h = thread_local(localdynamic) global i32 0
// LLVM: @i = thread_local(initialexec) global i32 0
// LLVM: @j = thread_local(localexec) global i32 0
// LLVM: @f.s = internal thread_local(initialexec) global i32 0
