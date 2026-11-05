// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -fmemory-profile %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fmemory-profile %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -fmemory-profile %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: -fmemory-profile emits MemProfProfileFilename module flag.

void f(void) {}

// CIR: cir.memprof_filename = "memprof.profraw"

// LLVM: !{i32 1, !"MemProfProfileFilename", !"memprof.profraw"}

// OGCG: !{i32 1, !"MemProfProfileFilename", !"memprof.profraw"}
