// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj \
// RUN:   -ftime-trace=%t.json -ftime-trace-granularity=0 %s -o %t.o
// RUN: cat %t.json \
// RUN:   | %python -c 'import json, sys; json.dump(json.loads(sys.stdin.read()), sys.stdout, sort_keys=True, indent=2)' \
// RUN:   | FileCheck %s

// CHECK-DAG: "name": "Frontend"
// CHECK-DAG: "name": "CIRGen AST Consumer"
// CHECK-DAG: "name": "CIR To CIR Passes"
// CHECK-DAG: "name": "CIR To LLVM Lowering"
// CHECK-DAG: "name": "LLVM Backend Emission"

int f(int x) { return x + 1; }
