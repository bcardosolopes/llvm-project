// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// cir.return parent op mismatch with unreachable goto cycles

int a;
int main() {
L1:
  goto L1;

L2:
  if (!a)
    goto L2;
}

// LLVM: define{{.*}} i32 @main()
