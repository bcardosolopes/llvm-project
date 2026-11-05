// RUN: %clang_cc1 -O2 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=NOOPT --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -O2 -finline-max-stacksize=64 -fclangir -emit-llvm %s -o %t-cir-opt.ll
// RUN: FileCheck --check-prefix=OPT --input-file=%t-cir-opt.ll %s
// RUN: %clang_cc1 -O2 -finline-max-stacksize=64 -fclangir -emit-cir %s -o %t-cir.mlir
// RUN: FileCheck --check-prefix=CIR --input-file=%t-cir.mlir %s
// RUN: %clang_cc1 -O2 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG-NOOPT --input-file=%t.ll %s
// RUN: %clang_cc1 -O2 -finline-max-stacksize=64 -emit-llvm %s -o %t-ogcg-opt.ll
// RUN: FileCheck --check-prefix=OGCG-OPT --input-file=%t-ogcg-opt.ll %s

void foo() {}

// NOOPT-NOT: inline-max-stacksize
// OPT:       define {{.*}}@foo{{.*}}#[[ATTR:[0-9]+]]
// OPT:       attributes #[[ATTR]] = {{.*}}"inline-max-stacksize"="64"
// CIR: #cir<extra({{.*}}"inline-max-stacksize" = "64"{{.*}})>

// OGCG-NOOPT-NOT: inline-max-stacksize
// OGCG-OPT:       define {{.*}}@foo{{.*}}#[[ATTR:[0-9]+]]
// OGCG-OPT:       attributes #[[ATTR]] = {{.*}}"inline-max-stacksize"="64"
