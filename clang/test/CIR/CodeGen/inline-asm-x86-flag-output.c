// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: llc %t.ll -o - >/dev/null

// CIR-LABEL: cir.func{{.*}} @check_no_clobber_conflicts()
// CIR: [[ASM:%.*]] = cir.asm(x86_att,
// CIR: {"" "={@cce},~{cx},~{dirflag},~{fpsr},~{flags}"}) {cir.asm_readnone} -> !u8i
// CIR-NEXT: [[UB:%.*]] = cir.const #cir.int<2> : !u8i
// CIR-NEXT: [[CMP:%.*]] = cir.cmp(lt, [[ASM]], [[UB]]) : !u8i, !cir.bool
// CIR-NEXT: cir.assume [[CMP]] : !cir.bool
// CIR: [[CAST:%.*]] = cir.cast bitcast {{.*}} : !cir.ptr<!cir.bool> -> !cir.ptr<!u8i>
// CIR-NEXT: cir.store align(1) [[ASM]], [[CAST]] : !u8i, !cir.ptr<!u8i>

// LLVM-LABEL: define{{.*}} zeroext i1 @check_no_clobber_conflicts()
// LLVM: [[ASM:%.*]] = {{tail call|call}} i8 asm "", "={@cce},~{cx},~{dirflag},~{fpsr},~{flags}"()
// LLVM-NEXT: [[CMP:%.*]] = icmp ult i8 [[ASM]], 2
// LLVM-NEXT: call void @llvm.assume(i1 [[CMP]])
// LLVM: store i8 [[ASM]]
// LLVM: [[LOAD:%.*]] = load i8
// LLVM-NEXT: [[BOOL:%.*]] = trunc i8 [[LOAD]] to i1
// LLVM: ret i1 [[BOOL]]
_Bool check_no_clobber_conflicts(void) {
  _Bool b;
  asm(""
      : "=@cce"(b)
      :
      : "cx");
  return b;
}
