// __attribute__((annotate)) survives the merge for every input, under the
// names the merged module gives its symbols.
//
// cir.global_annotations is a per-TU list whose entries name their symbol with
// a plain string. cir-link took the first input's list, dropping every other
// TU's annotations -- and an entry for a local that the merge renames would
// have kept pointing at the other TU's symbol of the old name.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/a.cpp -o %t/a.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/b.cpp -o %t/b.cir
// RUN: cir-link %t/a.cir %t/b.cir -o %t/ab.cir
// RUN: FileCheck --input-file=%t/ab.cir %s --check-prefix=CIR
// RUN: cir-link %t/a.cir %t/b.cir --emit-llvm --emit-llvm-text -o %t/ab.ll
// RUN: FileCheck --input-file=%t/ab.ll %s
//
// All four entries, b.cpp's helper under its merged name.
// CIR: cir.global_annotations = #cir<global_annotations [
// CIR-SAME: ["a_global", #cir.annotation<name = "from_a"
// CIR-SAME: ["_ZL6helperv", #cir.annotation<name = "helper_a"
// CIR-SAME: ["b_global", #cir.annotation<name = "from_b"
// CIR-SAME: ["_ZL6helperv.cirlink.1", #cir.annotation<name = "helper_b"
//
// CHECK: @llvm.global.annotations = appending global [4 x
// CHECK-SAME: ptr @a_global,
// CHECK-SAME: ptr @_ZL6helperv,
// CHECK-SAME: ptr @b_global,
// CHECK-SAME: ptr @_ZL6helperv.cirlink.1,

// A local that an external symbol arriving later displaces keeps its
// annotation under the name it moves to; the arriving symbol's annotation stays
// on the name it takes over.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/local.c -o %t/local.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/external.c -o %t/external.cir
// RUN: cir-link %t/local.cir %t/external.cir --emit-llvm --emit-llvm-text \
// RUN:     -o %t/aside.ll
// RUN: FileCheck --input-file=%t/aside.ll %s --check-prefix=ASIDE
// ASIDE-DAG: @[[LOCAL:[^ ]+]] = {{.*}}c"stays_local\00"
// ASIDE-DAG: @[[EXTERNAL:[^ ]+]] = {{.*}}c"is_external\00"
// ASIDE: @llvm.global.annotations = appending global [2 x
// ASIDE-SAME: ptr @foo.cirlink.1, ptr @[[LOCAL]],
// ASIDE-SAME: ptr @foo, ptr @[[EXTERNAL]],

//--- local.c
__attribute__((annotate("stays_local"))) static int foo(void) { return 1; }
int use_local(void) { return foo(); }

//--- external.c
__attribute__((annotate("is_external"))) int foo(void) { return 2; }

//--- a.cpp
extern "C" int a_global __attribute__((annotate("from_a"))) = 1;
__attribute__((annotate("helper_a"))) static int helper() { return 1; }
int use_a() { return helper(); }

//--- b.cpp
extern "C" int b_global __attribute__((annotate("from_b"))) = 2;
__attribute__((annotate("helper_b"))) static int helper() { return 2; }
int use_b() { return helper(); }
