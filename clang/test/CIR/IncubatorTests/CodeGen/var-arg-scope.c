// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-cir -mmlir --mlir-print-ir-before=cir-lowering-prepare %s -o %t.cir 2>&1 | FileCheck %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm -fno-clangir-call-conv-lowering %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

void f1(__builtin_va_list c) {
  { __builtin_va_arg(c, void *); }
}

// CIR: cir.func {{.*}} @f1(%arg0: !rec___va_list)
// CIR: [[VAR_LIST:%.*]] = cir.alloca !rec___va_list, !cir.ptr<!rec___va_list>, ["c", init] {alignment = 8 : i64}
// CIR: cir.store %arg0, [[VAR_LIST]] : !rec___va_list, !cir.ptr<!rec___va_list>
// CIR: cir.scope {
// CIR-NEXT: [[TMP:%.*]] = cir.va_arg [[VAR_LIST]] : (!cir.ptr<!rec___va_list>) -> !cir.ptr<!void>
// CIR-NEXT: }
// CIR-NEXT: cir.return

// LLVM: %struct.__va_list = type { ptr, ptr, ptr, i32, i32 }
// LLVM: define dso_local void @f1(%struct.__va_list %0)
// LLVM: [[VARLIST:%.*]] = alloca %struct.__va_list, i64 1, align 8
// LLVM: br label %[[SCOPE_FRONT:.*]]
// LLVM: [[SCOPE_FRONT]]:
// LLVM: [[GR_OFFS_P:%.*]] = getelementptr %struct.__va_list, ptr [[VARLIST]], i32 0, i32 3
// LLVM: [[GR_OFFS:%.*]] = load i32, ptr [[GR_OFFS_P]], align 4
// LLVM-NEXT: [[CMP0:%.*]] = icmp sge i32 [[GR_OFFS]], 0
// LLVM-NEXT: br i1 [[CMP0]], label %[[BB_ON_STACK:.*]], label %[[BB_MAY_REG:.*]]
// LLVM:  [[BB_MAY_REG]]: ;
// LLVM: [[NEW_REG_OFFS:%.*]] = add i32 [[GR_OFFS]], 8
// LLVM: store i32 [[NEW_REG_OFFS]], ptr [[GR_OFFS_P]], align 4
// LLVM-NEXT: [[CMP1:%.*]] = icmp sle i32 [[NEW_REG_OFFS]], 0
// LLVM-NEXT: br i1 [[CMP1]], label %[[BB_IN_REG:.*]], label %[[BB_ON_STACK]]
// LLVM:  [[BB_IN_REG]]: ;
// LLVM-NEXT: [[GR_TOP_P:%.*]] = getelementptr %struct.__va_list, ptr [[VARLIST]], i32 0, i32 1
// LLVM-NEXT: [[GR_TOP:%.*]] = load ptr, ptr [[GR_TOP_P]], align 8
// LLVM: br label %[[BB_END:.*]]
// LLVM:  [[BB_ON_STACK]]: ;
// LLVM-NEXT: [[STACK_P:%.*]] = getelementptr %struct.__va_list, ptr [[VARLIST]], i32 0, i32 0
// LLVM-NEXT: [[STACK_V:%.*]] = load ptr, ptr [[STACK_P]], align 8
// LLVM: br label %[[BB_END]]
// LLVM: [[BB_END]]: ; preds = %[[BB_ON_STACK]], %[[BB_IN_REG]]
// LLVM-NEXT: [[PHIP:%.*]] = phi ptr
// LLVM-NEXT: [[PHIV:%.*]] = load ptr, ptr [[PHIP]], align 8
// LLVM: br label %[[OUT_SCOPE:.*]]
// LLVM: [[OUT_SCOPE]]:
// LLVM-NEXT:  ret void
