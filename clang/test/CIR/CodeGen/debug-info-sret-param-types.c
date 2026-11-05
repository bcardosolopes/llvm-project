// Verify that sret-lowered functions preserve correct debug types for all
// parameters and locals. Previously the subroutine type included the sret
// pointer as an extra parameter, causing a +1 misalignment between the
// subroutine type entries and source-level parameters during record-type
// rewriting. This corrupted the type map and dropped types from unrelated
// local variables via replaceRewrittenTypes.
//
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -O0 -fclangir -emit-llvm \
// RUN:   -debug-info-kind=constructor -dwarf-version=4 -std=c99 -w \
// RUN:   -target-cpu generic -target-feature +neon %s -o - | FileCheck %s

typedef struct { int a; long b; int c; long d; } BigStruct;
typedef struct { int *data; int size; } DataStruct;

// Function returning a large struct (triggers sret lowering).
BigStruct make_big(DataStruct *d, int count, unsigned *workspace, int strategy) {
  BigStruct result = {0};
  result.a = count;
  result.b = strategy;
  if (d) {
    unsigned max = 35;
    result.c = max;
    result.d = d->size;
  }
  return result;
}

// Every DILocalVariable must carry a type: field — no null types allowed.
// CHECK-DAG: !DILocalVariable(name: "d", {{.*}}type:
// CHECK-DAG: !DILocalVariable(name: "count", {{.*}}type:
// CHECK-DAG: !DILocalVariable(name: "workspace", {{.*}}type:
// CHECK-DAG: !DILocalVariable(name: "strategy", {{.*}}type:
// CHECK-DAG: !DILocalVariable(name: "max", {{.*}}type:
