// RUN: %clang_cc1 -Wno-gcc-compat -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -Wno-gcc-compat -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that break/continue inside GCC statement expressions in for-loop
// step and condition positions are correctly lowered.

// Break in for-loop step (statement expression).
void break_in_step(int *p) {
  int j;
  for (j = 0; j < 10; (void)({ ++j; break; j; }))
    *p = 1;
}

// CIR-LABEL: @break_in_step
// CIR:   cir.for : cond
// CIR:   } body {
// CIR:   } step {
// CIR:     cir.scope {
// CIR:       cir.break

// LLVM-LABEL: @break_in_step
// LLVM:       store i32 1,
// The break in the step region should branch to the loop exit.
// LLVM:       br label %[[EXIT:[0-9]+]]
// LLVM:      {{^[0-9]+:}}
// LLVM-NOT:  br label %{{.*}}cond
// LLVM:       ret void

// Continue in for-loop step (statement expression).
void continue_in_step(int *p) {
  int j;
  for (j = 0; j < 10; (void)({ ++j; continue; j; }))
    *p = 2;
}

// CIR-LABEL: @continue_in_step
// CIR:   cir.for : cond
// CIR:   } body {
// CIR:   } step {
// CIR:     cir.scope {
// CIR:       cir.continue

// The continue in the step branches to the step (loop back to step start).
// LLVM-LABEL: @continue_in_step
// LLVM:       store i32 2,

// Break in for-loop condition (statement expression).
void break_in_cond(int *p) {
  int j;
  for (j = 0; ({break; j < 10;}); j++)
    *p = 3;
}

// CIR-LABEL: @break_in_cond
// CIR:   cir.for : cond {
// CIR:     cir.scope {
// CIR:       cir.break

// LLVM-LABEL: @break_in_cond

// Continue in for-loop condition (statement expression).
void continue_in_cond(int *p) {
  int j;
  for (j = 0; ({continue; j < 10;}); j++)
    *p = 4;
}

// CIR-LABEL: @continue_in_cond
// CIR:   cir.for : cond {
// CIR:     cir.scope {
// CIR:       cir.continue

// LLVM-LABEL: @continue_in_cond

// Break in while-loop condition (statement expression).
void break_in_while_cond(int *p, int x) {
  while (({break; --x;}))
    *p = 5;
}

// CIR-LABEL: @break_in_while_cond
// CIR:   cir.while {
// CIR:     cir.scope {
// CIR:       cir.break

// LLVM-LABEL: @break_in_while_cond

// Break in do-while condition (statement expression).
void break_in_dowhile_cond(int *p, int x) {
  do {
    *p = 6;
  } while (({break; --x;}));
}

// CIR-LABEL: @break_in_dowhile_cond
// CIR:   cir.do {
// CIR:   } while {
// CIR:     cir.scope {
// CIR:       cir.break

// LLVM-LABEL: @break_in_dowhile_cond
