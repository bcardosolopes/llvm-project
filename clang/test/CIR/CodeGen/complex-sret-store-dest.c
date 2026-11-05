// Regression test: on sret-return targets (i386, 32-bit ARM), a _Complex value
// is returned indirectly via a hidden sret pointer. When the call result is
// assigned directly into a non-local lvalue (here a pointer/array element whose
// address is loaded *after* the call), the call-convention lowering reuses that
// destination as the sret slot. It must ensure the destination pointer's
// defining op dominates the call; otherwise the lowered call gets a
// use-before-def sret operand and crashes during CIR -> LLVM lowering.

// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple armv7-none-linux-androideabi21 -fclangir -emit-llvm %s -o %t.arm.ll
// RUN: FileCheck --input-file=%t.arm.ll %s --check-prefix=LLVM

double _Complex mkc(double, double);

// The destination pointer is loaded before it is passed as the sret argument,
// so the sret operand dominates the call (no use-before-def / crash).
// LLVM-LABEL: define {{.*}}void @store
// LLVM: %[[DEST:.*]] = load ptr, ptr %{{.*}}
// LLVM: call void @mkc(ptr {{.*}}sret({ double, double }){{.*}} %[[DEST]], double {{.*}}, double {{.*}})
// LLVM: ret void
void store(double _Complex *a) { a[0] = mkc(1.0, 2.0); }

// Same for an indirect call through a function pointer: the sret destination
// pointer must dominate the (indirect) call.
double _Complex (*fp)(double, double);
// LLVM-LABEL: define {{.*}}void @store_indirect
// LLVM: %[[DESTI:.*]] = load ptr, ptr %{{.*}}
// LLVM: call void %{{.*}}(ptr {{.*}}sret({ double, double }){{.*}} %[[DESTI]], double {{.*}}, double {{.*}})
// LLVM: ret void
void store_indirect(double _Complex *a) { a[0] = fp(1.0, 2.0); }
