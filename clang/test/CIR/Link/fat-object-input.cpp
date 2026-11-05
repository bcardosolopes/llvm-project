// RUN: %clang -target x86_64-unknown-linux-gnu -fclangir -fclangir-fat-objects \
// RUN:   -O2 -c %s -o %t.o
// RUN: %clang -target x86_64-unknown-linux-gnu -fclangir -fclangir-fat-objects \
// RUN:   -O2 -c %S/Inputs/fat-object-input-b.cpp -o %t-b.o
// RUN: cir-link %t.o %t-b.o -o %t.cir
// RUN: cir-opt %t.cir | FileCheck %s
// An object built without -fclangir-fat-objects carries no CIR. That is by
// far the likeliest mistake, so it gets its own diagnostic rather than a
// confusing parse error.
// RUN: %clang -target x86_64-unknown-linux-gnu -O2 -c %s -o %t-plain.o
// RUN: not cir-link %t-plain.o -o %t-plain.cir 2>&1 | FileCheck %s --check-prefix=MISSING

// cir-link takes object files directly and reads the .cir section itself.
//
// That is the form a link-time pipeline actually receives: the build system
// hands over the same .o list it would hand the linker. Requiring the sections
// to be extracted first would mean one extra action per object, which at a
// real library is hundreds of them -- 354 in one real library.

int helper(int x);

int caller(int x) { return helper(x) * 2; }

// Both translation units are present in the merged module, so the sections
// were found and merged rather than one input being silently skipped.
// CHECK-DAG: cir.func{{.*}}@_Z6callee
// CHECK-DAG: cir.func{{.*}}@_Z6calleri

// MISSING: plain.o:0:0: error: object file has no .cir section; was it built with -fclangir-fat-objects?
