// Code the CIR inliner moves keeps its provenance: a DILocation in the
// callee's DISubprogram, inlined at the call -- the inline frame crash
// symbolization and AutoFDO read. A callee that also stays out of line shares
// one DISubprogram with its inlined copies.
//
// RUN: %clang_cc1 -triple aarch64-linux-android28 -fclangir -O2 \
// RUN:     -fclangir-mem2reg -fclangir-inline -disable-llvm-passes \
// RUN:     -fcxx-exceptions -fexceptions \
// RUN:     -debug-info-kind=limited -dwarf-version=4 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=DECL

int work(int);

// Inlined into its only caller, then dropped.
static int helper(int x) {
  return work(x * 2); // HELPER-CALL
}
int caller(int y) {
  return helper(y) + 1; // CALLER-CALL
}
// CHECK-LABEL: define {{.*}} @_Z6calleri(
// CHECK:         call {{.*}}@_Z4worki({{.*}}), !dbg ![[IN_HELPER:[0-9]+]]

// Small and external: inlined, and kept out of line. (No parameter: under
// debug info CIR mem2reg does not run, and a spilled one counts toward size.)
int tiny() {
  return work(7); // TINY-CALL
}
int caller2(int y) {
  return tiny() + y; // CALLER2-CALL
}
// CHECK: define {{.*}} @_Z4tinyv({{.*}}) {{.*}}!dbg ![[TINY_SP:[0-9]+]]
// CHECK-LABEL: define {{.*}} @_Z7caller2i(
// CHECK:         call {{.*}}@_Z4worki({{.*}}), !dbg ![[IN_TINY:[0-9]+]]

// A throw inlined from a dropped callee: lowering declares __cxa_throw with
// the throw's location, inlined frame and all, but the declaration keeps a
// subprogram of its own rather than taking the callee's (the verifier rejects
// one subprogram on two functions).
[[noreturn]] static void fail() { throw 1; }
int checked(int x) {
  if (x < 0)
    fail();
  return x;
}
// CHECK-LABEL: define {{.*}} @_Z7checkedi(
// CHECK-NOT:     call {{.*}}@_ZL4failv
// CHECK:         call void @__cxa_throw({{.*}}){{.*}}, !dbg ![[IN_FAIL:[0-9]+]]
// DECL: declare {{.*}}!dbg ![[THROW_SP:[0-9]+]] {{.*}}@__cxa_throw(
// DECL: ![[THROW_SP]] = !DISubprogram(name: "__cxa_throw"

// CHECK-DAG: ![[IN_HELPER]] = !DILocation(line: 17, {{.*}}scope: ![[HELPER_SP:[0-9]+]], inlinedAt: ![[AT_CALLER:[0-9]+]])
// CHECK-DAG: ![[HELPER_SP]] = distinct !DISubprogram(name: "_ZL6helperi"
// CHECK-DAG: ![[AT_CALLER]] = !DILocation(line: 20, {{.*}}scope: ![[CALLER_SP:[0-9]+]])
// CHECK-DAG: ![[CALLER_SP]] = distinct !DISubprogram({{.*}}linkageName: "_Z6calleri"
// CHECK-DAG: ![[IN_TINY]] = !DILocation(line: 28, {{.*}}scope: ![[TINY_SP]], inlinedAt: ![[AT_CALLER2:[0-9]+]])
// CHECK-DAG: ![[AT_CALLER2]] = !DILocation(line: 31,
// CHECK-DAG: ![[IN_FAIL]] = !DILocation({{.*}}scope: ![[FAIL_SP:[0-9]+]], inlinedAt:
// CHECK-DAG: ![[FAIL_SP]] = distinct !DISubprogram(name: "_ZL4failv"
