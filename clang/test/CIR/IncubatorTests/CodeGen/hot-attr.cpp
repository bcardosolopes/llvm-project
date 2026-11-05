// RUN: %clang_cc1 -O2 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -O2 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

__attribute__((hot)) int s0(int a, int b) {
  int x = a + b;
  return x;
}

// CIR:      #[[ATTR0:.+]] = #cir<extra({{{.*}}hot = #cir.hot
// CIR:      cir.func {{.*}} @_Z2s0ii(
// CIR-SAME:     -> (!s32i {cir.noundef}) extra(#[[ATTR0]])

// LLVM: define dso_local noundef i32 @_Z2s0ii({{.*}} #[[#ATTR1:]] {
// LLVM: attributes #[[#ATTR1]] = {{.*}} hot

