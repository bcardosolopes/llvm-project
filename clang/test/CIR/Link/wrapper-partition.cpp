// How cir-link-wrapper splits a link command line, checked by giving it a
// "linker" that just prints what it was handed.
//
// This is the part most likely to break silently: everything here is about
// which arguments reach the linker and in what order, and a mistake produces
// a link that still succeeds while meaning something different.
//
// The negatives are --implicit-check-not, not CHECK-NOT: a CHECK-NOT is only
// matched against the region after the preceding CHECK, and since /bin/echo
// prints one line these would all search an empty region and never fail.
//
// REQUIRES: system-linux

// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang -fclangir -fclangir-fat-objects --target=%itanium_abi_triple \
// RUN:     -c %s -o %t/fat.o
// RUN: %clang --target=%itanium_abi_triple -c %s -o %t/plain.o

//--- With no CIR anywhere the linker runs untouched: no merged object is
//    added, and the arguments arrive exactly as given.
// RUN: cir-link-wrapper --linker-path=/bin/echo \
// RUN:     %t/plain.o -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PASSTHRU --implicit-check-not=merged.bc
// PASSTHRU: {{.*}}plain.o -o {{.*}}out.so -shared

//--- A value that follows a separate-value option is not an input, even when
//    it names an object that carries CIR. Handing -o a path that happens to
//    be a fat object must not move it into the merged set and leave the
//    linker a dangling -o.
// RUN: cp %t/fat.o %t/output-slot.o
// RUN: cir-link-wrapper --linker-path=/bin/echo \
// RUN:     %t/plain.o -o %t/output-slot.o -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OUTPUT-SLOT
// OUTPUT-SLOT: -o {{.*}}output-slot.o

//--- Objects in a lazy group keep archive-member semantics: absorbing one
//    would link it unconditionally.
// RUN: cir-link-wrapper --linker-path=/bin/echo \
// RUN:     --start-lib %t/fat.o --end-lib %t/plain.o -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LAZY --implicit-check-not=merged.bc
// LAZY: --start-lib {{.*}}fat.o --end-lib

//--- An eagerly-linked fat object is replaced, and the merged bitcode takes
//    the slot it occupied rather than being appended after the libraries.
//
// Bitcode, not an object: the constituents it stands in for reached the linker
// as bitcode too, and the linker's LTO is what optimizes them. Nothing in the
// CIR path runs an LLVM optimization pipeline, so there is no compile here and
// no clang to point at -- only the triple, which cir-link needs to give the
// merged module a target.
// RUN: cir-link-wrapper --linker-path=/bin/echo \
// RUN:     --cir-triple=%itanium_abi_triple \
// RUN:     %t/fat.o -lc -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SLOT --implicit-check-not=fat.o
// SLOT: merged.bc -lc

struct S {
  int x;
};
int g(S *s) { return s->x; }
