// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// REQUIRES: x86-registered-target

// An `asm goto` with a register output stores the asm result into the output
// slot only on the fallthrough edge in CIR; the stores on the indirect (label)
// edges are materialized later, in split blocks, during LLVM-IR translation.
// CIR mem2reg runs before that, so promoting the output slot would compute the
// reaching definition on the indirect edge from the pre-asm value and drop the
// asm output on the branch-taken edge. The output slot must therefore survive
// CIR mem2reg with its callbr-result stores on both the fallthrough and the
// split (indirect) edge; LLVM's own mem2reg promotes it afterwards. The label
// path must observe the asm output, not the pre-asm input.

int single_output(int cond) {
  asm goto("addl %0, %0; jnz %l[nz];" : "+r"(cond) : : "cc" : nz);
  return 100;
nz:
  return cond;
}

// LLVM-LABEL: @single_output
// LLVM: %[[CBR:.*]] = callbr i32 asm
// The split (indirect) edge stores the asm result, not the pre-asm input.
// LLVM: split:
// LLVM-NEXT: store i32 %[[CBR]], ptr

int multi_label(int cond) {
  asm goto("addl %0, %0; jz %l[z]; jmp %l[nz];" : "+r"(cond) : : "cc" : z, nz);
  return 100;
z:
  return 200;
nz:
  return cond;
}

// LLVM-LABEL: @multi_label
// LLVM: %[[MCBR:.*]] = callbr i32 asm
// Both indirect edges get a split block that stores the asm result.
// LLVM: split{{[0-9]*}}:
// LLVM-NEXT: store i32 %[[MCBR]], ptr
// LLVM: split{{[0-9]*}}:
// LLVM-NEXT: store i32 %[[MCBR]], ptr
