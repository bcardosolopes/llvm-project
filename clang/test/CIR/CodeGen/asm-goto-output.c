// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.og.ll
// REQUIRES: x86-registered-target

// Asm goto with output operands and multiple labels.

int single_output(int cond) {
  asm volatile goto("testl %0, %0; jne %l2;" : "=r"(cond) : "r"(cond) :: lab1, lab2);
  return 0;
lab2:
  return 0;
lab1:
  return 1;
}

// CIR: %[[RESULT:.*]] = cir.asm.goto
// CIR: -> !s32i
// CIR: cir.store {{.*}}%[[RESULT]]

// The callbr must have i32 result type, and split blocks must store the result.
// LLVM: %[[CBR:.*]] = callbr i32 asm sideeffect
// LLVM-SAME: "testl $0, $0; jne ${2:l};"
// LLVM: store i32 %[[CBR]], ptr
// LLVM: store i32 %[[CBR]], ptr
// LLVM: store i32 %[[CBR]], ptr

// OGCG: %[[OGCBR:.*]] = callbr i32 asm sideeffect
// OGCG-SAME: "testl $0, $0; jne ${2:l};"
// OGCG: store i32 %[[OGCBR]], ptr
// OGCG: store i32 %[[OGCBR]], ptr
// OGCG: store i32 %[[OGCBR]], ptr

int two_asm_gotos(int cond) {
  asm volatile goto("testl %0, %0; jne %l2;" : "=r"(cond) : "r"(cond) :: lab1, lab2);
  asm volatile goto("testl %0, %0; jne %l3;" : "=r"(cond) : "r"(cond) :: lab1, lab2);
  return 0;
lab2:
  return 0;
lab1:
  return 1;
}

// CIR: cir.func {{.*}}@two_asm_gotos
// CIR: cir.asm.goto
// CIR: -> !s32i
// CIR: cir.asm.goto
// CIR: -> !s32i

// LLVM: define {{.*}} @two_asm_gotos
// LLVM: callbr i32 asm sideeffect
// LLVM: callbr i32 asm sideeffect

// Multi-output: split blocks must extractvalue each field and store to
// individual output allocas, not just store the whole struct to the temp.
int multi_output(int in1) {
  int out1, out2;
  asm volatile goto("testl %0, %0; jne %l3;"
    : "=r"(out1), "=r"(out2) : "r"(in1) :: label);
  return out1 + out2;
label:
  return out1 + out2 + 100;
}

// CIR: cir.func {{.*}}@multi_output
// CIR: cir.asm.goto
// CIR: -> !rec_anon_struct

// Verify that the split block has extractvalue+store for each output field,
// matching OGCG. Previously, CIR only stored the whole struct to a temp
// alloca in the split block, leaving individual output allocas uninitialized.
// LLVM: define {{.*}} @multi_output
// LLVM: %[[MCBR:.*]] = callbr { i32, i32 } asm sideeffect
// LLVM: .split:
// LLVM-NEXT: %[[V0:.*]] = extractvalue { i32, i32 } %[[MCBR]], 0
// LLVM-NEXT: store i32 %[[V0]], ptr
// LLVM-NEXT: %[[V1:.*]] = extractvalue { i32, i32 } %[[MCBR]], 1
// LLVM-NEXT: store i32 %[[V1]], ptr

// OGCG: define {{.*}} @multi_output
// OGCG: callbr { i32, i32 } asm sideeffect
// OGCG: .split:
// OGCG-NEXT: %[[OV0:.*]] = extractvalue { i32, i32 }
// OGCG-NEXT: %[[OV1:.*]] = extractvalue { i32, i32 }
// OGCG-NEXT: store i32 %[[OV0]], ptr
// OGCG-NEXT: store i32 %[[OV1]], ptr

// Narrowed outputs must replay the same truncation/store sequence on the
// indirect-label split block as on the fallthrough path.
int single_output_narrowed(int in) {
  signed char out = 0;
  asm goto("" : "=r"(out) : "0"(in) : : lab);
  return out;
lab:
  return out + 1;
}

// LLVM-LABEL: @single_output_narrowed
// LLVM: %[[SCBR:.*]] = callbr i32 asm "", "=r,0,!i,~{dirflag},~{fpsr},~{flags}"(i32 %{{.*}})
// LLVM: %[[STRUNC:.*]] = trunc i32 %[[SCBR]] to i8
// LLVM: store i8 %[[STRUNC]], ptr
// LLVM: .split:
// LLVM-NEXT: %[[STRUNC_SPLIT:.*]] = trunc i32 %[[SCBR]] to i8
// LLVM-NEXT: store i8 %[[STRUNC_SPLIT]], ptr

// OGCG-LABEL: @single_output_narrowed
// OGCG: %[[OSCBR:.*]] = callbr i32 asm "", "=r,0,!i,~{dirflag},~{fpsr},~{flags}"(i32 %{{.*}})
// OGCG: %[[OSTRUNC:.*]] = trunc i32 %[[OSCBR]] to i8
// OGCG: store i8 %[[OSTRUNC]], ptr
// OGCG: lab.split:
// OGCG-NEXT: %[[OSTRUNC_SPLIT:.*]] = trunc i32 %[[OSCBR]] to i8
// OGCG-NEXT: store i8 %[[OSTRUNC_SPLIT]], ptr

int multi_output_narrowed(int in1, int in2) {
  signed char a = 0;
  int b = 0;
  asm goto("" : "=r"(a), "=r"(b) : "0"(in1), "1"(in2) : : lab);
  return a + b;
lab:
  return a + b + 1;
}

// LLVM-LABEL: @multi_output_narrowed
// LLVM: %[[MCBR2:.*]] = callbr { i32, i32 } asm "", "=r,=r,0,1,!i,~{dirflag},~{fpsr},~{flags}"(i32 %{{.*}}, i32 %{{.*}})
// LLVM: .split:
// LLVM-NEXT: %[[MV0:.*]] = extractvalue { i32, i32 } %[[MCBR2]], 0
// LLVM-NEXT: %[[MTRUNC:.*]] = trunc i32 %[[MV0]] to i8
// LLVM-NEXT: store i8 %[[MTRUNC]], ptr
// LLVM-NEXT: %[[MV1:.*]] = extractvalue { i32, i32 } %[[MCBR2]], 1
// LLVM-NEXT: store i32 %[[MV1]], ptr

// OGCG-LABEL: @multi_output_narrowed
// OGCG: %[[OMCBR2:.*]] = callbr { i32, i32 } asm "", "=r,=r,0,1,!i,~{dirflag},~{fpsr},~{flags}"(i32 %{{.*}}, i32 %{{.*}})
// OGCG: lab.split:
// OGCG: %[[OMV0:.*]] = extractvalue { i32, i32 } %[[OMCBR2]], 0
// OGCG: %[[OMV1:.*]] = extractvalue { i32, i32 } %[[OMCBR2]], 1
// OGCG: %[[OMTRUNC:.*]] = trunc i32 %[[OMV0]] to i8
// OGCG: store i8 %[[OMTRUNC]], ptr
// OGCG: store i32 %[[OMV1]], ptr
