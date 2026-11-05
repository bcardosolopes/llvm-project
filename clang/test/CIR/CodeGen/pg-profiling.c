// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -pg %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=MCOUNT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -pg -mfentry %s -o %t-fentry.ll
// RUN: FileCheck --input-file=%t-fentry.ll %s --check-prefix=FENTRY

// Verify that -pg adds "instrument-function-entry-inlined"="mcount" and
// -pg -mfentry adds "fentry-call"="true".
// Matches OGCG CodeGenFunction.cpp:1174-1196.

void f(void) {}

// MCOUNT: attributes {{.*}} "instrument-function-entry-inlined"="mcount"
// FENTRY: attributes {{.*}} "fentry-call"="true"
// FENTRY-NOT: mcount
