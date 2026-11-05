// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -fforbid-guard-variables %s 2>&1 | FileCheck --check-prefix=CIR %s
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -fforbid-guard-variables %s -o /dev/null 2>&1 | \
// RUN:   FileCheck --check-prefix=OGCG %s

int extfn();

void foo() {
  static int x = extfn();
}

// CIR: error: this initialization requires a guard variable, which the kernel does not support
// OGCG: error: this initialization requires a guard variable, which the kernel does not support
