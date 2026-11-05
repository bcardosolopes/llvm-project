// cir-link --partitions=N lowers the merged module in N pieces that together
// define everything the merged module does, each definition exactly once: an
// internal function stays in the piece of the function that calls it, the
// global constructor list is in the first piece only, and a piece declares
// what it calls in another -- a function alias like the function it aliases,
// argument attributes included.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %t/a.cpp -o %t/a.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %t/b.cpp -o %t/b.o
// RUN: cir-link %t/a.o %t/b.o --partitions=2 --emit-llvm --emit-llvm-text \
// RUN:     -o %t/out.ll
// RUN: cat %t/out.ll %t/out.ll.1 > %t/all.ll
// RUN: FileCheck --input-file=%t/all.ll %s --check-prefix=SCALE
// RUN: FileCheck --input-file=%t/all.ll %s --check-prefix=TWICE
// RUN: FileCheck --input-file=%t/all.ll %s --check-prefix=USE
// RUN: FileCheck --input-file=%t/all.ll %s --check-prefix=CTORS
// RUN: FileCheck --input-file=%t/all.ll %s --check-prefix=ALIAS
// RUN: FileCheck --input-file=%t/out.ll %s --check-prefix=FIRST
// RUN: grep -l '^define .*@_Z5scalei(' %t/out.ll %t/out.ll.1 > %t/scale.txt
// RUN: grep -l '^define internal .*@_ZL5twicei(' %t/out.ll %t/out.ll.1 \
// RUN:     > %t/twice.txt
// RUN: diff %t/scale.txt %t/twice.txt
//
// SCALE-COUNT-1: define {{.*}}@_Z5scalei(
// SCALE-NOT:     define {{.*}}@_Z5scalei(
// TWICE-COUNT-1: define internal {{.*}}@_ZL5twicei(
// TWICE-NOT:     define {{.*}}@_ZL5twicei(
// USE-COUNT-1:   define {{.*}}@_Z3usei(
// USE-NOT:       define {{.*}}@_Z3usei(
// CTORS-COUNT-1: @llvm.global_ctors =
// CTORS-NOT:     @llvm.global_ctors =
// FIRST: @llvm.global_ctors =
// ALIAS-DAG: @widen_alias = {{.*}}alias i16 (i16), ptr @widen
// ALIAS-DAG: declare {{.*}}i16 @widen_alias(i16 noundef)

//--- a.cpp
__attribute__((noinline)) static int twice(int x) { return x * 2; }
int scale(int x) { return twice(x) + 1; }
struct Counter {
  Counter();
  int n;
};
Counter::Counter() : n(scale(3)) {}
Counter counter;
extern "C" __attribute__((noinline)) short widen(short v) { return v + 1; }
extern "C" short widen_alias(short) __attribute__((alias("widen")));

//--- b.cpp
int scale(int);
int use(int y) { return scale(y) * 3; }
extern "C" short widen_alias(short);
short (*aliasPtr())(short) { return &widen_alias; }
