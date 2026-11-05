// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// i386 previously fell through to the AArch64 CIRGen target-info class (whose
// setTargetAttributes no-ops on x86), silently dropping the x86 interrupt
// calling convention and force_align_arg_pointer stack realignment. A dedicated
// X8632TargetCIRGenInfo now applies these.

__attribute__((force_align_arg_pointer)) int aligned_fn(int x) { return x * 2; }
// CHECK: define{{.*}}@aligned_fn{{.*}} #[[ALIGNATTR:[0-9]+]]

struct iframe { unsigned ip, cs, flags, sp, ss; };
__attribute__((interrupt)) void handler(struct iframe *f) { (void)f; }
// CHECK: define{{.*}}x86_intrcc{{.*}}@handler

// CHECK: attributes #[[ALIGNATTR]] = {{.*}}"stackrealign"
