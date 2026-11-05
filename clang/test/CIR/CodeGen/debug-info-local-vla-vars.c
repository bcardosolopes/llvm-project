// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test for a compile-time optimization: local variable debug-info
// emission in DirectToLLVM now consumes tracked named alloca candidates instead
// of full llvm.func walks. Keep dynamic allocas (VLA) covered.

int local_vla_sum(int n) {
  int base = n + 1;
  int vla[n];
  vla[0] = base;
  return vla[0];
}

// CHECK-LABEL: define{{.*}} @local_vla_sum
// CHECK-DAG: #dbg_declare(ptr %{{[0-9]+}}, !{{[0-9]+}}, !DIExpression()
// CHECK-DAG: #dbg_declare(ptr %{{[0-9]+}}, !{{[0-9]+}}, !DIExpression()
// CHECK-DAG: #dbg_declare(ptr %{{[0-9]+}}, !{{[0-9]+}}, !DIExpression()
// CHECK-DAG: !DILocalVariable(name: "n",{{.*}}arg: 1,
// CHECK-DAG: !DILocalVariable(name: "base",
// CHECK-DAG: !DILocalVariable(name: "vla",
