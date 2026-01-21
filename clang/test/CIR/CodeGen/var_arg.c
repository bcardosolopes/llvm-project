// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// CIR: !rec___va_list_tag = !cir.record<struct "__va_list_tag" {!u32i, !u32i, !cir.ptr<!void>, !cir.ptr<!void>}
// LLVM: %struct.__va_list_tag = type { i32, i32, ptr, ptr }
// OGCG: %struct.__va_list_tag = type { i32, i32, ptr, ptr }

int varargs(int count, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, count);
    int res = __builtin_va_arg(args, int);
    __builtin_va_end(args);
    return res;
}

// CIR-LABEL: cir.func {{.*}} @varargs(
// CIR:   %[[COUNT_ADDR:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["count", init]
// CIR:   %[[RET_ADDR:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["__retval"]
// CIR:   %[[VAAREA:.+]] = cir.alloca !cir.array<!rec___va_list_tag x 1>, !cir.ptr<!cir.array<!rec___va_list_tag x 1>>, ["args"]
// CIR:   %[[RES_ADDR:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["res", init]
// CIR:   cir.store %arg0, %[[COUNT_ADDR]] : !s32i, !cir.ptr<!s32i>
// CIR:   %[[VA_PTR0:.+]] = cir.cast array_to_ptrdecay %[[VAAREA]] : !cir.ptr<!cir.array<!rec___va_list_tag x 1>> -> !cir.ptr<!rec___va_list_tag>
// CIR:   %[[COUNT_VAL:.+]] = cir.load{{.*}} %[[COUNT_ADDR]] : !cir.ptr<!s32i>, !s32i
// CIR:   cir.va_start %[[VA_PTR0]] %[[COUNT_VAL]] : !cir.ptr<!rec___va_list_tag>, !s32i
// CIR:   %[[VA_PTR1:.+]] = cir.cast array_to_ptrdecay %[[VAAREA]] : !cir.ptr<!cir.array<!rec___va_list_tag x 1>> -> !cir.ptr<!rec___va_list_tag>
// CIR:   %[[VA_ARG:.+]] = cir.scope {
// CIR:     %[[GP_OFFSET_PTR:.+]] = cir.get_member %[[VA_PTR1]][0] {name = "gp_offset"} : !cir.ptr<!rec___va_list_tag> -> !cir.ptr<!u32i>
// CIR:     %[[GP_OFFSET:.+]] = cir.load %[[GP_OFFSET_PTR]] : !cir.ptr<!u32i>, !u32i
// CIR:     %[[LIMIT:.+]] = cir.const #cir.int<40> : !u32i
// CIR:     %[[CMP:.+]] = cir.cmp(le, %[[GP_OFFSET]], %[[LIMIT]]) : !u32i, !cir.bool
// CIR:     cir.brcond %[[CMP]] ^[[IN_REG:.+]], ^[[IN_MEM:.+]] loc
//
// CIR:   ^[[IN_REG]]:
// CIR:     %[[REG_SAVE_AREA_PTR:.+]] = cir.get_member %[[VA_PTR1]][3] {name = "reg_save_area"} : !cir.ptr<!rec___va_list_tag> -> !cir.ptr<!cir.ptr<!void>>
// CIR:     %[[REG_SAVE_AREA:.+]] = cir.load %[[REG_SAVE_AREA_PTR]] : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
// CIR:     %[[CUR_ADDR:.+]] = cir.ptr_stride %[[REG_SAVE_AREA]], %[[GP_OFFSET]] : (!cir.ptr<!void>, !u32i) -> !cir.ptr<!void>
// CIR:     %[[EIGHT:.+]] = cir.const #cir.int<8> : !u32i
// CIR:     %[[NEW_OFFSET:.+]] = cir.binop(add, %[[GP_OFFSET]], %[[EIGHT]]) : !u32i
// CIR:     cir.store %[[NEW_OFFSET]], %[[GP_OFFSET_PTR]] : !u32i, !cir.ptr<!u32i>
// CIR:     cir.br ^[[CONT:.+]](%[[CUR_ADDR]] : !cir.ptr<!void>)
//
// CIR:   ^[[IN_MEM]]:
// CIR:     %[[OVERFLOW_PTR:.+]] = cir.get_member %[[VA_PTR1]][2] {name = "overflow_arg_area"} : !cir.ptr<!rec___va_list_tag> -> !cir.ptr<!cir.ptr<!void>>
// CIR:     %[[OVERFLOW:.+]] = cir.load %[[OVERFLOW_PTR]] : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
// CIR:     %[[STRIDE:.+]] = cir.const #cir.int<8> : !s32i
// CIR:     %[[OVERFLOW_I8:.+]] = cir.cast bitcast %[[OVERFLOW]] : !cir.ptr<!void> -> !cir.ptr<!s8i>
// CIR:     %[[NEW_OVERFLOW_I8:.+]] = cir.ptr_stride %[[OVERFLOW_I8]], %[[STRIDE]] : (!cir.ptr<!s8i>, !s32i) -> !cir.ptr<!s8i>
// CIR:     %[[NEW_OVERFLOW:.+]] = cir.cast bitcast %[[NEW_OVERFLOW_I8]] : !cir.ptr<!s8i> -> !cir.ptr<!void>
// CIR:     cir.store %[[NEW_OVERFLOW]], %[[OVERFLOW_PTR]] : !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>
// CIR:     cir.br ^[[CONT]](%[[OVERFLOW]] : !cir.ptr<!void>)
//
// CIR:   ^[[CONT]](%[[ARG_ADDR:.+]]: !cir.ptr<!void>
// CIR:     %[[CAST:.+]] = cir.cast bitcast %[[ARG_ADDR]] : !cir.ptr<!void> -> !cir.ptr<!s32i>
// CIR:     %[[LOADED:.+]] = cir.load align(8) %[[CAST]] : !cir.ptr<!s32i>, !s32i
// CIR:     cir.yield %[[LOADED]] : !s32i
// CIR:   } : !s32i
// CIR:   cir.store{{.*}} %[[VA_ARG]], %[[RES_ADDR]] : !s32i, !cir.ptr<!s32i>
// CIR:   %[[VA_PTR2:.+]] = cir.cast array_to_ptrdecay %[[VAAREA]] : !cir.ptr<!cir.array<!rec___va_list_tag x 1>> -> !cir.ptr<!rec___va_list_tag>
// CIR:   cir.va_end %[[VA_PTR2]] : !cir.ptr<!rec___va_list_tag>
// CIR:   %[[RESULT:.+]] = cir.load{{.*}} %[[RES_ADDR]] : !cir.ptr<!s32i>, !s32i
// CIR:   cir.store %[[RESULT]], %[[RET_ADDR]] : !s32i, !cir.ptr<!s32i>
// CIR:   %[[RETVAL:.+]] = cir.load{{.*}} %[[RET_ADDR]] : !cir.ptr<!s32i>, !s32i
// CIR:   cir.return %[[RETVAL]] : !s32i

// LLVM-LABEL: define dso_local i32 @varargs(
// LLVM:   %[[COUNT_ADDR:.+]] = alloca i32{{.*}}
// LLVM:   %[[RET_ADDR:.+]] = alloca i32{{.*}}
// LLVM:   %[[VAAREA:.+]] = alloca [1 x %struct.__va_list_tag]{{.*}}
// LLVM:   %[[RES_ADDR:.+]] = alloca i32{{.*}}
// LLVM:   %[[VA_PTR0:.+]] = getelementptr %struct.__va_list_tag, ptr %[[VAAREA]], i32 0
// LLVM:   call void @llvm.va_start.p0(ptr %[[VA_PTR0]])
// LLVM:   %[[VA_PTR1:.+]] = getelementptr %struct.__va_list_tag, ptr %[[VAAREA]], i32 0
// LLVM:   %[[GP_OFFSET_PTR:.+]] = getelementptr {{.*}} %[[VA_PTR1]], i32 0, i32 0
// LLVM:   %[[GP_OFFSET:.+]] = load i32, ptr %[[GP_OFFSET_PTR]]
// LLVM:   %[[CMP:.+]] = icmp ule i32 %[[GP_OFFSET]], 40
// LLVM:   br i1 %[[CMP]], label %[[IN_REG:.+]], label %[[IN_MEM:.+]]
//
// LLVM: [[IN_REG]]:
// LLVM:   %[[UPDATED:.+]] = add i32 %[[GP_OFFSET]], 8
// LLVM:   store i32 %[[UPDATED]], ptr %[[GP_OFFSET_PTR]]
// LLVM:   br label %[[CONT:.+]]
//
// LLVM: [[IN_MEM]]:
// LLVM:   %[[OVERFLOW_PTR:.+]] = getelementptr {{.*}} %[[VA_PTR1]], i32 0, i32 2
// LLVM:   %[[OVERFLOW:.+]] = load ptr, ptr %[[OVERFLOW_PTR]]
// LLVM:   %[[NEW_OVERFLOW:.+]] = getelementptr {{.*}} %[[OVERFLOW]], i64 8
// LLVM:   store ptr %[[NEW_OVERFLOW]], ptr %[[OVERFLOW_PTR]]
// LLVM:   br label %[[CONT]]
//
// LLVM: [[CONT]]:
// LLVM:   %[[VA_PTR2:.+]] = getelementptr %struct.__va_list_tag, ptr %[[VAAREA]], i32 0
// LLVM:   call void @llvm.va_end.p0(ptr %[[VA_PTR2]])
// LLVM:   ret i32

// OGCG-LABEL: define dso_local i32 @varargs
// OGCG:   %[[COUNT_ADDR:.+]] = alloca i32
// OGCG:   %[[VAAREA:.+]] = alloca [1 x %struct.__va_list_tag]
// OGCG:   %[[RES_ADDR:.+]] = alloca i32
// OGCG:   %[[DECAY:.+]] = getelementptr inbounds [1 x %struct.__va_list_tag], ptr %[[VAAREA]]
// OGCG:   call void @llvm.va_start.p0(ptr %[[DECAY]])
// OGCG:   %[[DECAY1:.+]] = getelementptr inbounds [1 x %struct.__va_list_tag], ptr %[[VAAREA]]
// OGCG:   %[[GPOFFSET_PTR:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %[[DECAY1]], i32 0, i32 0
// OGCG:   %[[GPOFFSET:.+]] = load i32, ptr %[[GPOFFSET_PTR]]
// OGCG:   %[[COND:.+]] = icmp ule i32 %[[GPOFFSET]], 40
// OGCG:   br i1 %[[COND]], label %vaarg.in_reg, label %vaarg.in_mem
//
// OGCG: vaarg.in_reg:
// OGCG:   %[[REGSAVE_PTR:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %[[DECAY1]], i32 0, i32 3
// OGCG:   %[[REGSAVE:.+]] = load ptr, ptr %[[REGSAVE_PTR]]
// OGCG:   %[[VAADDR1:.+]] = getelementptr i8, ptr %[[REGSAVE]], i32 %[[GPOFFSET]]
// OGCG:   br label %vaarg.end
//
// OGCG: vaarg.in_mem:
// OGCG:   %[[OVERFLOW_PTR:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %[[DECAY1]], i32 0, i32 2
// OGCG:   %[[OVERFLOW:.+]] = load ptr, ptr %[[OVERFLOW_PTR]]
// OGCG:   br label %vaarg.end
//
// OGCG: vaarg.end:
// OGCG:   %[[PHI:.+]] = phi ptr [ %[[VAADDR1]], %vaarg.in_reg ], [ %[[OVERFLOW]], %vaarg.in_mem ]
// OGCG:   %[[LOADED:.+]] = load i32, ptr %[[PHI]]
// OGCG:   store i32 %[[LOADED]], ptr %[[RES_ADDR]]
// OGCG:   %[[DECAY2:.+]] = getelementptr inbounds [1 x %struct.__va_list_tag], ptr %[[VAAREA]]
// OGCG:   call void @llvm.va_end.p0(ptr %[[DECAY2]])
// OGCG:   %[[VAL:.+]] = load i32, ptr %[[RES_ADDR]]
// OGCG:   ret i32 %[[VAL]]

int stdarg_start(int count, ...) {
    __builtin_va_list args;
    __builtin_stdarg_start(args, 12345);
    int res = __builtin_va_arg(args, int);
    __builtin_va_end(args);
    return res;
}

// CIR-LABEL: cir.func {{.*}} @stdarg_start(
// CIR:   %[[COUNT_ADDR:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["count", init]
// CIR:   %[[RET_ADDR:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["__retval"]
// CIR:   %[[VAAREA:.+]] = cir.alloca !cir.array<!rec___va_list_tag x 1>, !cir.ptr<!cir.array<!rec___va_list_tag x 1>>, ["args"]
// CIR:   %[[RES_ADDR:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["res", init]
// CIR:   cir.store %arg0, %[[COUNT_ADDR]] : !s32i, !cir.ptr<!s32i>
// CIR:   %[[VA_PTR0:.+]] = cir.cast array_to_ptrdecay %[[VAAREA]] : !cir.ptr<!cir.array<!rec___va_list_tag x 1>> -> !cir.ptr<!rec___va_list_tag>
// CIR:   %[[C12345:.+]] = cir.const #cir.int<12345> : !s32i
// CIR:   cir.va_start %[[VA_PTR0]] %[[C12345]] : !cir.ptr<!rec___va_list_tag>, !s32i
// CIR:   %[[VA_PTR1:.+]] = cir.cast array_to_ptrdecay %[[VAAREA]] : !cir.ptr<!cir.array<!rec___va_list_tag x 1>> -> !cir.ptr<!rec___va_list_tag>
// CIR:   %[[VA_ARG:.+]] = cir.scope {
// CIR:     %[[GP_OFFSET_PTR:.+]] = cir.get_member %[[VA_PTR1]][0] {name = "gp_offset"} : !cir.ptr<!rec___va_list_tag> -> !cir.ptr<!u32i>
// CIR:     %[[GP_OFFSET:.+]] = cir.load %[[GP_OFFSET_PTR]] : !cir.ptr<!u32i>, !u32i
// CIR:     %[[LIMIT:.+]] = cir.const #cir.int<40> : !u32i
// CIR:     %[[CMP:.+]] = cir.cmp(le, %[[GP_OFFSET]], %[[LIMIT]]) : !u32i, !cir.bool
// CIR:     cir.brcond %[[CMP]] ^[[IN_REG:.+]], ^[[IN_MEM:.+]] loc
//
// CIR:   ^[[IN_REG]]:
// CIR:     %[[REG_SAVE_AREA_PTR:.+]] = cir.get_member %[[VA_PTR1]][3] {name = "reg_save_area"} : !cir.ptr<!rec___va_list_tag> -> !cir.ptr<!cir.ptr<!void>>
// CIR:     %[[REG_SAVE_AREA:.+]] = cir.load %[[REG_SAVE_AREA_PTR]] : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
// CIR:     %[[CUR_ADDR:.+]] = cir.ptr_stride %[[REG_SAVE_AREA]], %[[GP_OFFSET]] : (!cir.ptr<!void>, !u32i) -> !cir.ptr<!void>
// CIR:     %[[EIGHT:.+]] = cir.const #cir.int<8> : !u32i
// CIR:     %[[NEW_OFFSET:.+]] = cir.binop(add, %[[GP_OFFSET]], %[[EIGHT]]) : !u32i
// CIR:     cir.store %[[NEW_OFFSET]], %[[GP_OFFSET_PTR]] : !u32i, !cir.ptr<!u32i>
// CIR:     cir.br ^[[CONT:.+]](%[[CUR_ADDR]] : !cir.ptr<!void>)
//
// CIR:   ^[[IN_MEM]]:
// CIR:     %[[OVERFLOW_PTR:.+]] = cir.get_member %[[VA_PTR1]][2] {name = "overflow_arg_area"} : !cir.ptr<!rec___va_list_tag> -> !cir.ptr<!cir.ptr<!void>>
// CIR:     %[[OVERFLOW:.+]] = cir.load %[[OVERFLOW_PTR]] : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
// CIR:     %[[STRIDE:.+]] = cir.const #cir.int<8> : !s32i
// CIR:     %[[OVERFLOW_I8:.+]] = cir.cast bitcast %[[OVERFLOW]] : !cir.ptr<!void> -> !cir.ptr<!s8i>
// CIR:     %[[NEW_OVERFLOW_I8:.+]] = cir.ptr_stride %[[OVERFLOW_I8]], %[[STRIDE]] : (!cir.ptr<!s8i>, !s32i) -> !cir.ptr<!s8i>
// CIR:     %[[NEW_OVERFLOW:.+]] = cir.cast bitcast %[[NEW_OVERFLOW_I8]] : !cir.ptr<!s8i> -> !cir.ptr<!void>
// CIR:     cir.store %[[NEW_OVERFLOW]], %[[OVERFLOW_PTR]] : !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>
// CIR:     cir.br ^[[CONT]](%[[OVERFLOW]] : !cir.ptr<!void>)
//
// CIR:   ^[[CONT]](%[[ARG_ADDR:.+]]: !cir.ptr<!void>
// CIR:     %[[CAST:.+]] = cir.cast bitcast %[[ARG_ADDR]] : !cir.ptr<!void> -> !cir.ptr<!s32i>
// CIR:     %[[LOADED:.+]] = cir.load align(8) %[[CAST]] : !cir.ptr<!s32i>, !s32i
// CIR:     cir.yield %[[LOADED]] : !s32i
// CIR:   } : !s32i
// CIR:   cir.store{{.*}} %[[VA_ARG]], %[[RES_ADDR]] : !s32i, !cir.ptr<!s32i>
// CIR:   %[[VA_PTR2:.+]] = cir.cast array_to_ptrdecay %[[VAAREA]] : !cir.ptr<!cir.array<!rec___va_list_tag x 1>> -> !cir.ptr<!rec___va_list_tag>
// CIR:   cir.va_end %[[VA_PTR2]] : !cir.ptr<!rec___va_list_tag>
// CIR:   %[[RESULT:.+]] = cir.load{{.*}} %[[RES_ADDR]] : !cir.ptr<!s32i>, !s32i
// CIR:   cir.store %[[RESULT]], %[[RET_ADDR]] : !s32i, !cir.ptr<!s32i>
// CIR:   %[[RETVAL:.+]] = cir.load{{.*}} %[[RET_ADDR]] : !cir.ptr<!s32i>, !s32i
// CIR:   cir.return %[[RETVAL]] : !s32i

// LLVM-LABEL: define dso_local i32 @stdarg_start(
// LLVM:   %[[COUNT_ADDR:.+]] = alloca i32{{.*}}
// LLVM:   %[[RET_ADDR:.+]] = alloca i32{{.*}}
// LLVM:   %[[VAAREA:.+]] = alloca [1 x %struct.__va_list_tag]{{.*}}
// LLVM:   %[[RES_ADDR:.+]] = alloca i32{{.*}}
// LLVM:   %[[VA_PTR0:.+]] = getelementptr %struct.__va_list_tag, ptr %[[VAAREA]], i32 0
// LLVM:   call void @llvm.va_start.p0(ptr %[[VA_PTR0]])
// LLVM:   %[[VA_PTR1:.+]] = getelementptr %struct.__va_list_tag, ptr %[[VAAREA]], i32 0
// LLVM:   %[[GP_OFFSET_PTR:.+]] = getelementptr {{.*}} %[[VA_PTR1]], i32 0, i32 0
// LLVM:   %[[GP_OFFSET:.+]] = load i32, ptr %[[GP_OFFSET_PTR]]
// LLVM:   %[[CMP:.+]] = icmp ule i32 %[[GP_OFFSET]], 40
// LLVM:   br i1 %[[CMP]], label %[[IN_REG:.+]], label %[[IN_MEM:.+]]
//
// LLVM: [[IN_REG]]:
// LLVM:   %[[UPDATED:.+]] = add i32 %[[GP_OFFSET]], 8
// LLVM:   store i32 %[[UPDATED]], ptr %[[GP_OFFSET_PTR]]
// LLVM:   br label %[[CONT:.+]]
//
// LLVM: [[IN_MEM]]:
// LLVM:   %[[OVERFLOW_PTR:.+]] = getelementptr {{.*}} %[[VA_PTR1]], i32 0, i32 2
// LLVM:   %[[OVERFLOW:.+]] = load ptr, ptr %[[OVERFLOW_PTR]]
// LLVM:   %[[NEW_OVERFLOW:.+]] = getelementptr {{.*}} %[[OVERFLOW]], i64 8
// LLVM:   store ptr %[[NEW_OVERFLOW]], ptr %[[OVERFLOW_PTR]]
// LLVM:   br label %[[CONT]]
//
// LLVM: [[CONT]]:
// LLVM:   %[[VA_PTR2:.+]] = getelementptr %struct.__va_list_tag, ptr %[[VAAREA]], i32 0
// LLVM:   call void @llvm.va_end.p0(ptr %[[VA_PTR2]])
// LLVM:   ret i32

// OGCG-LABEL: define dso_local i32 @stdarg_start
// OGCG:   %[[COUNT_ADDR:.+]] = alloca i32
// OGCG:   %[[VAAREA:.+]] = alloca [1 x %struct.__va_list_tag]
// OGCG:   %[[RES_ADDR:.+]] = alloca i32
// OGCG:   %[[DECAY:.+]] = getelementptr inbounds [1 x %struct.__va_list_tag], ptr %[[VAAREA]], i64 0, i64 0
// OGCG:   call void @llvm.va_start.p0(ptr %[[DECAY]])
// OGCG:   %[[DECAY1:.+]] = getelementptr inbounds [1 x %struct.__va_list_tag], ptr %[[VAAREA]], i64 0, i64 0
// OGCG:   %[[GPOFFSET_PTR:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %[[DECAY1]], i32 0, i32 0
// OGCG:   %[[GPOFFSET:.+]] = load i32, ptr %[[GPOFFSET_PTR]]
// OGCG:   %[[COND:.+]] = icmp ule i32 %[[GPOFFSET]], 40
// OGCG:   br i1 %[[COND]], label %vaarg.in_reg, label %vaarg.in_mem
//
// OGCG: vaarg.in_reg:
// OGCG:   %[[REGSAVE_PTR:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %[[DECAY1]], i32 0, i32 3
// OGCG:   %[[REGSAVE:.+]] = load ptr, ptr %[[REGSAVE_PTR]]
// OGCG:   %[[VAADDR1:.+]] = getelementptr i8, ptr %[[REGSAVE]], i32 %[[GPOFFSET]]
// OGCG:   %[[NEXT_GPOFFSET:.+]] = add i32 %[[GPOFFSET]], 8
// OGCG:   store i32 %[[NEXT_GPOFFSET]], ptr %[[GPOFFSET_PTR]]
// OGCG:   br label %vaarg.end
//
// OGCG: vaarg.in_mem:
// OGCG:   %[[OVERFLOW_PTR:.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %[[DECAY1]], i32 0, i32 2
// OGCG:   %[[OVERFLOW:.+]] = load ptr, ptr %[[OVERFLOW_PTR]]
// OGCG:   %[[OVERFLOW_NEXT:.+]] = getelementptr i8, ptr %[[OVERFLOW]], i32 8
// OGCG:   store ptr %[[OVERFLOW_NEXT]], ptr %[[OVERFLOW_PTR]]
// OGCG:   br label %vaarg.end
//
// OGCG: vaarg.end:
// OGCG:   %[[PHI:.+]] = phi ptr [ %[[VAADDR1]], %vaarg.in_reg ], [ %[[OVERFLOW]], %vaarg.in_mem ]
// OGCG:   %[[LOADED:.+]] = load i32, ptr %[[PHI]]
// OGCG:   store i32 %[[LOADED]], ptr %[[RES_ADDR]]
// OGCG:   %[[DECAY2:.+]] = getelementptr inbounds [1 x %struct.__va_list_tag], ptr %[[VAAREA]], i64 0, i64 0
// OGCG:   call void @llvm.va_end.p0(ptr %[[DECAY2]])
// OGCG:   %[[VAL:.+]] = load i32, ptr %[[RES_ADDR]]
// OGCG:   ret i32 %[[VAL]]

void stdarg_copy() {
    __builtin_va_list src, dest;
    __builtin_va_copy(src, dest);
}

// CIR-LABEL: @stdarg_copy
// CIR:    %{{.*}} = cir.cast array_to_ptrdecay %{{.*}} : !cir.ptr<!cir.array<!rec___va_list_tag x 1>> -> !cir.ptr<!rec___va_list_tag>
// CIR:    %{{.*}} = cir.cast array_to_ptrdecay %{{.*}} : !cir.ptr<!cir.array<!rec___va_list_tag x 1>> -> !cir.ptr<!rec___va_list_tag>
// CIR:    cir.va_copy %{{.*}} to %{{.*}} : !cir.ptr<!rec___va_list_tag>, !cir.ptr<!rec___va_list_tag>

// LLVM-LABEL: @stdarg_copy
// LLVM:   %{{.*}} = getelementptr %struct.__va_list_tag, ptr %{{.*}}
// LLVM:   %{{.*}} = getelementptr %struct.__va_list_tag, ptr %{{.*}}
// LLVM:   call void @llvm.va_copy.p0(ptr %{{.*}}, ptr %{{.*}}

// OGCG-LABEL: @stdarg_copy
// OGCG:   %{{.*}} = getelementptr inbounds [1 x %struct.__va_list_tag], ptr %{{.*}}
// OGCG:   %{{.*}} = getelementptr inbounds [1 x %struct.__va_list_tag], ptr %{{.*}}
// OGCG:   call void @llvm.va_copy.p0(ptr %{{.*}}, ptr %{{.*}}
