// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR

int f(int x) { return x + 1; }

// Ensure location materialization still emits file/line/col locations.
// CIR: #loc1 = loc("[[FILE:.*location-cache.c]]":4:1)
// CIR: #loc2 = loc("[[FILE]]":4:30)
// CIR: cir.func{{.*}} @f
