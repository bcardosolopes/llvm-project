// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -debug-info-kind=constructor -dwarf-version=4 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

int s0(int a, int b) {
  int x = a + b;
  if (x > 0)
    x = 0;
  else
    x = 1;
  return x;
}

// CIR: #loc1 = loc("{{.*}}sourcelocation.cpp":6:1)
// CIR: #loc2 = loc("{{.*}}sourcelocation.cpp":13:1)
// CIR: #loc[[FUNC_LOC:[0-9]+]] = loc(fused[#loc1, #loc2])
// CIR: module @"{{.*}}sourcelocation.cpp" attributes {{{.*}}cir.lang = #cir.lang<cxx>, {{.*}}cir.sob = #cir.signed_overflow_behavior<undefined>
// CIR:   cir.func {{.*}} @_Z2s0ii(%arg0: !s32i {cir.noundef} loc(fused[#loc1, #loc2]), %arg1: !s32i {cir.noundef} loc(fused[#loc1, #loc2])) -> (!s32i {cir.noundef})
// CIR:     %0 = cir.alloca !s32i, !cir.ptr<!s32i>, ["a", init] {alignment = 4 : i64} loc(#loc[[A_INIT_LOC:[0-9]+]])
// CIR:     %1 = cir.alloca !s32i, !cir.ptr<!s32i>, ["b", init] {alignment = 4 : i64} loc(#loc[[B_INIT_LOC:[0-9]+]])
// CIR:     %2 = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init] {alignment = 4 : i64} loc(#loc[[X_DECL_LOC:[0-9]+]])
// CIR:     cir.store{{.*}} %arg0, %0 : !s32i, !cir.ptr<!s32i> loc(#loc[[ARG_STORE_LOC:[0-9]+]])
// CIR:     cir.store{{.*}} %arg1, %1 : !s32i, !cir.ptr<!s32i> loc(#loc[[ARG_STORE_LOC]])
// CIR:     %3 = cir.load{{.*}} %0 : !cir.ptr<!s32i>, !s32i loc(#loc[[ADD_LHS_LOC:[0-9]+]])
// CIR:     %4 = cir.load{{.*}} %1 : !cir.ptr<!s32i>, !s32i loc(#loc[[ADD_RHS_LOC:[0-9]+]])
// CIR:     %5 = cir.binop(add, %3, %4) nsw : !s32i loc(#loc[[ADD_LOC:[0-9]+]])
// CIR:     cir.store{{.*}} %5, %2 : !s32i, !cir.ptr<!s32i> loc(#loc[[X_DECL_LOC]])
// CIR:     %6 = cir.load{{.*}} %2 : !cir.ptr<!s32i>, !s32i loc(#loc[[COND_LOAD_LOC:[0-9]+]])
// CIR:     %7 = cir.const #cir.int<0> : !s32i loc(#loc[[ZERO_LOC:[0-9]+]])
// CIR:     %8 = cir.cmp(gt, %6, %7) : !s32i, !cir.bool loc(#loc[[CMP_LOC:[0-9]+]])
// CIR:     cir.if %8 {
// CIR:       %10 = cir.const #cir.int<0> : !s32i loc(#loc[[THEN_CONST_LOC:[0-9]+]])
// CIR:       cir.store{{.*}} %10, %2 : !s32i, !cir.ptr<!s32i> loc(#loc[[THEN_STORE_LOC:[0-9]+]])
// CIR:     } else {
// CIR:       %10 = cir.const #cir.int<1> : !s32i loc(#loc[[ELSE_CONST_LOC:[0-9]+]])
// CIR:       cir.store{{.*}} %10, %2 : !s32i, !cir.ptr<!s32i> loc(#loc[[ELSE_STORE_LOC:[0-9]+]])
// CIR:     } loc(#loc[[IF_LOC:[0-9]+]])
// CIR:     %9 = cir.load{{.*}} %2 : !cir.ptr<!s32i>, !s32i loc(#loc[[RETVAL_SRC_LOC:[0-9]+]])
// CIR:     cir.return %9 : !s32i loc(#loc[[RET_LOC:[0-9]+]])
// CIR:   } loc(#loc[[FUNC_LOC]])
// CIR: } loc(#loc)
// CIR: #loc = loc("{{.*}}sourcelocation.cpp":0:0)
// CIR: #loc3 = loc("{{.*}}sourcelocation.cpp":6:8)
// CIR: #loc4 = loc("{{.*}}sourcelocation.cpp":6:12)
// CIR: #loc5 = loc("{{.*}}sourcelocation.cpp":6:15)
// CIR: #loc6 = loc("{{.*}}sourcelocation.cpp":6:19)
// CIR: #loc[[X_DECL_LOC]] = loc("{{.*}}sourcelocation.cpp":7:7)
// CIR: #loc[[ARG_STORE_LOC]] = loc("{{.*}}sourcelocation.cpp":6:22)
// CIR: #loc[[ADD_LHS_LOC]] = loc("{{.*}}sourcelocation.cpp":7:11)
// CIR: #loc[[ADD_RHS_LOC]] = loc("{{.*}}sourcelocation.cpp":7:15)
// CIR: #loc[[COND_LOAD_LOC]] = loc("{{.*}}sourcelocation.cpp":8:7)
// CIR: #loc[[ZERO_LOC]] = loc("{{.*}}sourcelocation.cpp":8:11)
// CIR: #loc[[CMP_LOC]] = loc("{{.*}}sourcelocation.cpp":8:9)
// CIR: #loc[[THEN_SCOPE_LOC:[0-9]+]] = loc("{{.*}}sourcelocation.cpp":9:5)
// CIR: #loc[[THEN_VALUE_LOC:[0-9]+]] = loc("{{.*}}sourcelocation.cpp":9:9)
// CIR: #loc[[ELSE_SCOPE_LOC:[0-9]+]] = loc("{{.*}}sourcelocation.cpp":11:5)
// CIR: #loc[[ELSE_VALUE_LOC:[0-9]+]] = loc("{{.*}}sourcelocation.cpp":11:9)
// CIR: #loc[[THEN_OP_LOC:[0-9]+]] = loc("{{.*}}sourcelocation.cpp":9:7)
// CIR: #loc[[ELSE_OP_LOC:[0-9]+]] = loc("{{.*}}sourcelocation.cpp":11:7)
// CIR: #loc[[RETVAL_SRC_LOC]] = loc("{{.*}}sourcelocation.cpp":12:10)
// CIR: #loc[[RET_LOC]] = loc("{{.*}}sourcelocation.cpp":12:3)
// CIR: #loc[[A_INIT_LOC]] = loc(fused[#loc3, #loc4])
// CIR: #loc[[B_INIT_LOC]] = loc(fused[#loc5, #loc6])
// CIR: #loc[[ADD_LOC]] = loc(fused[#loc[[ADD_LHS_LOC]], #loc[[ADD_RHS_LOC]]])
// CIR: #loc[[IF_LOC]] = loc(fused[#loc[[THEN_SCOPE_LOC]], #loc[[THEN_VALUE_LOC]], #loc[[ELSE_SCOPE_LOC]], #loc[[ELSE_VALUE_LOC]]])
// CIR: #loc[[THEN_CONST_LOC]] = loc(fused<{llvm.di_scope_chain = [9 : i32, 5 : i32], llvm.di_scope_column = 5 : i32, llvm.di_scope_line = 9 : i32}>[#loc[[THEN_VALUE_LOC]]])
// CIR: #loc[[THEN_STORE_LOC]] = loc(fused<{llvm.di_scope_chain = [9 : i32, 5 : i32], llvm.di_scope_column = 5 : i32, llvm.di_scope_line = 9 : i32}>[#loc[[THEN_OP_LOC]]])
// CIR: #loc[[ELSE_CONST_LOC]] = loc(fused<{llvm.di_scope_chain = [11 : i32, 5 : i32], llvm.di_scope_column = 5 : i32, llvm.di_scope_line = 11 : i32}>[#loc[[ELSE_VALUE_LOC]]])
// CIR: #loc[[ELSE_STORE_LOC]] = loc(fused<{llvm.di_scope_chain = [11 : i32, 5 : i32], llvm.di_scope_column = 5 : i32, llvm.di_scope_line = 11 : i32}>[#loc[[ELSE_OP_LOC]]])


// LLVM: ModuleID = '{{.*}}sourcelocation.cpp'
// LLVM: source_filename = "{{.*}}sourcelocation.cpp"
// LLVM: define dso_local noundef i32 @_Z2s0ii(i32 noundef %0, i32 noundef %1) #[[#]] !dbg ![[#SP:]]
// LLVM:  %3 = alloca i32, i64 1, align 4, !dbg ![[#LOC1:]]


// LLVM-DAG: !llvm.module.flags = !{![[#]]
// LLVM-DAG: !llvm.dbg.cu = !{![[#]]}

// LLVM: ![[#CU:]] = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: ![[#CUFILE:]], producer: "{{.*}}clang{{.*}}", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false, nameTableKind: None)
// LLVM: ![[#CUFILE]] = !DIFile(filename: "{{.*}}CodeGen/<stdin>", directory: "")
// LLVM: ![[#SP]] = distinct !DISubprogram(name: "s0", linkageName: "_Z2s0ii", scope: ![[SRCFILE:[0-9]+]], file: ![[SRCFILE]], line: 6, type: ![[#SRTY:]], scopeLine: 6, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: ![[#CU]], retainedNodes: ![[#]])
// LLVM: ![[SRCFILE]] = !DIFile(filename: "sourcelocation.cpp", directory: {{.*}}CodeGen{{.*}})
// LLVM: ![[#SRTY]] = !DISubroutineType(types: ![[#]])
