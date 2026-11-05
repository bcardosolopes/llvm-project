// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -fclangir -emit-llvm %s -o - | FileCheck %s

// ARM __attribute__((interrupt(...))) must emit the "interrupt"="<mode>" fn
// attribute the backend uses for the interrupt return (subs pc, lr, #N).
// Previously ARMTargetCIRGenInfo had no setTargetAttributes and dropped it.

__attribute__((interrupt("IRQ"))) void irq(void) {}
// CHECK: define{{.*}}@irq(){{.*}} #[[IRQ:[0-9]+]]

__attribute__((interrupt("FIQ"))) void fiq(void) {}
// CHECK: define{{.*}}@fiq(){{.*}} #[[FIQ:[0-9]+]]

// CHECK: attributes #[[IRQ]] = {{.*}}"interrupt"="IRQ"
// CHECK: attributes #[[FIQ]] = {{.*}}"interrupt"="FIQ"
