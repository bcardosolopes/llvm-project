// Test patchable-function-entry attribute emission.

// --- Test 1: Per-function attributes ---
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// --- Test 2: CLI flag -fpatchable-function-entry ---
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -fpatchable-function-entry=3 -emit-llvm %s -o %t.cli.ll
// RUN: FileCheck --input-file=%t.cli.ll %s --check-prefix=CLI
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu \
// RUN:     -fpatchable-function-entry=3 -emit-llvm %s -o %t.cli.ogcg.ll
// RUN: FileCheck --input-file=%t.cli.ogcg.ll %s --check-prefix=CLI
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -fpatchable-function-entry=3 -emit-cir %s -o %t.cli.cir
// RUN: FileCheck --input-file=%t.cli.cir %s --check-prefix=CIR

// patchable_function_entry(0) — no patchable attributes emitted.
void f0(void) __attribute__((patchable_function_entry(0))) {}
// LLVM: define {{.*}}void @f0() #[[PLAIN:[0-9]+]]
// OGCG: define {{.*}}void @f0() #[[PLAIN:[0-9]+]]

// patchable_function_entry(2) — 2 nop entries, no prefix.
void f2(void) __attribute__((patchable_function_entry(2))) {}
// LLVM: define {{.*}}void @f2() #[[PFE2:[0-9]+]]
// OGCG: define {{.*}}void @f2() #[[PFE2:[0-9]+]]

// patchable_function_entry(4, 4) — all 4 as prefix, 0 as entry.
void f44(void) __attribute__((patchable_function_entry(4, 4))) {}
// LLVM: define {{.*}}void @f44() #[[PFE44:[0-9]+]]
// OGCG: define {{.*}}void @f44() #[[PFE44:[0-9]+]]

// patchable_function_entry(5, 2) — 2 as prefix, 3 as entry.
void f52(void) __attribute__((patchable_function_entry(5, 2))) {}
// LLVM: define {{.*}}void @f52() #[[PFE52:[0-9]+]]
// OGCG: define {{.*}}void @f52() #[[PFE52:[0-9]+]]

// Plain function — no patchable attributes.
void plain(void) {}
// LLVM: define {{.*}}void @plain() #[[PLAIN]]
// OGCG: define {{.*}}void @plain() #[[PLAIN]]

// LLVM-DAG: attributes #[[PFE2]] = {{{.*}}"patchable-function-entry"="2"
// LLVM-DAG: attributes #[[PFE44]] = {{{.*}}"patchable-function-entry"="0" "patchable-function-prefix"="4"
// LLVM-DAG: attributes #[[PFE52]] = {{{.*}}"patchable-function-entry"="3" "patchable-function-prefix"="2"

// OGCG-DAG: attributes #[[PFE2]] = {{{.*}}"patchable-function-entry"="2"
// OGCG-DAG: attributes #[[PFE44]] = {{{.*}}"patchable-function-entry"="0" "patchable-function-prefix"="4"
// OGCG-DAG: attributes #[[PFE52]] = {{{.*}}"patchable-function-entry"="3" "patchable-function-prefix"="2"

// CLI flag: all functions get patchable-function-entry="3", attribute(0) overrides.
// CLI: define {{.*}}void @f0() #[[CLIPLAIN:[0-9]+]]
// CLI: define {{.*}}void @f2() #[[CLIPFE2:[0-9]+]]
// CLI: define {{.*}}void @f44() #[[CLIPFE44:[0-9]+]]
// CLI: define {{.*}}void @f52() #[[CLIPFE52:[0-9]+]]
// CLI: define {{.*}}void @plain() #[[CLIPFE3:[0-9]+]]
// CLI-DAG: attributes #[[CLIPFE3]] = {{{.*}}"patchable-function-entry"="3"
// CLI-DAG: attributes #[[CLIPFE2]] = {{{.*}}"patchable-function-entry"="2"
// CLI-DAG: attributes #[[CLIPFE44]] = {{{.*}}"patchable-function-entry"="0" "patchable-function-prefix"="4"
// CLI-DAG: attributes #[[CLIPFE52]] = {{{.*}}"patchable-function-entry"="3" "patchable-function-prefix"="2"

// CIR-DAG: "patchable-function-entry" = "3", "stack-protector-buffer-size" = "8"
// CIR-DAG: "patchable-function-entry" = "2"
// CIR-DAG: "patchable-function-entry" = "0", "patchable-function-prefix" = "4"
// CIR-DAG: "patchable-function-entry" = "3", "patchable-function-prefix" = "2"
