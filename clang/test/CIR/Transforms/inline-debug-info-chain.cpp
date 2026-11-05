// Code inlined through a chain of calls keeps every frame of it: the call to
// work, moved from leaf through mid and top into entry, is in leaf's
// DISubprogram, inlined at mid's call, inlined at top's, inlined at entry's.
//
// RUN: %clang_cc1 -triple aarch64-linux-android28 -fclangir -O2 \
// RUN:     -fclangir-mem2reg -fclangir-inline -disable-llvm-passes \
// RUN:     -debug-info-kind=limited -dwarf-version=4 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

int work(int);

static int leaf(int x) {
  return work(x * 2); // LEAF-CALL
}
static int mid(int x) {
  return leaf(x + 3) - 1; // MID-CALL
}
static int top(int x) {
  return mid(x ^ 5) * 7; // TOP-CALL
}
int entry(int y) {
  return top(y) + 1; // ENTRY-CALL
}

// CHECK-LABEL: define {{.*}} @_Z5entryi(
// CHECK-NOT:     call {{.*}}@_ZL
// CHECK:         call {{.*}}@_Z4worki({{.*}}), !dbg ![[IN_LEAF:[0-9]+]]

// CHECK-DAG: ![[IN_LEAF]] = !DILocation(line: 13, column: 10, scope: ![[LEAF_SP:[0-9]+]], inlinedAt: ![[AT_MID:[0-9]+]])
// CHECK-DAG: ![[LEAF_SP]] = distinct !DISubprogram(name: "_ZL4leafi"
// CHECK-DAG: ![[AT_MID]] = !DILocation(line: 16, column: 10, scope: ![[MID_SP:[0-9]+]], inlinedAt: ![[AT_TOP:[0-9]+]])
// CHECK-DAG: ![[MID_SP]] = distinct !DISubprogram(name: "_ZL3midi"
// CHECK-DAG: ![[AT_TOP]] = !DILocation(line: 19, column: 10, scope: ![[TOP_SP:[0-9]+]], inlinedAt: ![[AT_ENTRY:[0-9]+]])
// CHECK-DAG: ![[TOP_SP]] = distinct !DISubprogram(name: "_ZL3topi"
// CHECK-DAG: ![[AT_ENTRY]] = !DILocation(line: 22, column: 10, scope: ![[ENTRY_SP:[0-9]+]])
// CHECK-DAG: ![[ENTRY_SP]] = distinct !DISubprogram(name: "entry"
