// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fno-clangir-call-conv-lowering %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fno-clangir-call-conv-lowering %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

#include <stdarg.h>

double f1(int n, ...) {
  va_list valist;
  va_start(valist, n);
  double res = va_arg(valist, double);
  va_end(valist);
  return res;
}

// CHECK: [[VA_LIST_TYPE:%.+]] = type { i32, i32, ptr, ptr }

// CHECK: define {{.*}}@f1
// CHECK: [[VA_LIST_ALLOCA:%.+]] = alloca {{.*}}[[VA_LIST_TYPE]]
// CHECK: [[VA_LIST:%.+]] = getelementptr {{.*}} [[VA_LIST_ALLOCA]], i32 0
// CHECK: call {{.*}}@llvm.va_start.p0(ptr [[VA_LIST]])
// CHECK: [[VA_LIST2:%.+]] = getelementptr {{.*}} [[VA_LIST_ALLOCA]], i32 0
// CHECK: [[FP_OFFSET_P:%.+]] = getelementptr inbounds nuw {{.*}} [[VA_LIST2]], i32 0, i32 1
// CHECK: [[FP_OFFSET:%.+]] = load {{.*}}, ptr [[FP_OFFSET_P]]
// CHECK: [[COMPARED:%.+]] = icmp ule i32 {{.*}}, 160
// CHECK: br i1 [[COMPARED]], label %[[THEN_BB:.+]], label %[[ELSE_BB:.+]]
//
// CHECK: [[THEN_BB]]:
// CHECK:   [[UPDATED_FP_OFFSET:%.+]] = add i32 [[FP_OFFSET]], 16
// CHECK:   store i32 [[UPDATED_FP_OFFSET]], ptr [[FP_OFFSET_P]]
// CHECK:   br label %[[CONT_BB:.+]]
//
// CHECK: [[ELSE_BB]]:
// CHECK:   [[OVERFLOW_ARG_AREA_ADDR:%.+]] = getelementptr inbounds nuw {{.*}} [[VA_LIST2]], i32 0, i32 2
// CHECK:   [[OVERFLOW_ARG_AREA:%.+]] = load ptr, ptr [[OVERFLOW_ARG_AREA_ADDR]]
// CHECK:   [[OVERFLOW_ARG_AREA_OFFSET:%.+]] = getelementptr inbounds {{.*}} [[OVERFLOW_ARG_AREA]], i64 8
// CHECK:   store ptr [[OVERFLOW_ARG_AREA_OFFSET]], ptr [[OVERFLOW_ARG_AREA_ADDR]]
// CHECK:   br label %[[CONT_BB]]
//
// CHECK: [[CONT_BB]]:
// CHECK: [[VA_LIST3:%.+]] = getelementptr {{.*}} [[VA_LIST_ALLOCA]], i32 0
// CHECK: call {{.*}}@llvm.va_end.p0(ptr [[VA_LIST3]])

// CIR: cir.func {{.*}} @f1
// CIR: [[VA_LIST_ALLOCA:%.+]] = cir.alloca !cir.array<!rec___va_list_tag x 1>,
// CIR: [[RES:%.+]] = cir.alloca !cir.double, !cir.ptr<!cir.double>, ["res",
// CIR: [[VASTED_VA_LIST:%.+]] = cir.cast array_to_ptrdecay [[VA_LIST_ALLOCA]]
// CIR: cir.va_start [[VASTED_VA_LIST]]
// CIR: [[VASTED_VA_LIST:%.+]] = cir.cast array_to_ptrdecay [[VA_LIST_ALLOCA]]
// CIR: [[VAARG_RESULT:%.+]] = cir.scope
// CIR: [[FP_OFFSET_P:%.+]] = cir.get_member [[VASTED_VA_LIST]][1] {name = "fp_offset"}
// CIR: [[FP_OFFSET:%.+]] = cir.load align(4) [[FP_OFFSET_P]]
// CIR: [[OFFSET_CONSTANT:%.+]] = cir.const #cir.int<160>
// CIR: [[CMP:%.+]] = cir.cmp(le, [[FP_OFFSET]], [[OFFSET_CONSTANT]])
// CIR: cir.brcond [[CMP]] ^[[InRegBlock:.+]], ^[[InMemBlock:.+]] loc
//
// CIR: ^[[InRegBlock]]:
// CIR: [[REG_SAVE_AREA_P:%.+]] = cir.get_member [[VASTED_VA_LIST]][3] {name = "reg_save_area"}
// CIR: [[REG_SAVE_AREA:%.+]] = cir.load align(16) [[REG_SAVE_AREA_P]]
// CIR: [[UPDATED:%.+]] = cir.ptr_stride inbounds [[REG_SAVE_AREA]], [[FP_OFFSET]] : (!cir.ptr<!void>, !u32i) -> !cir.ptr<!void>
// CIR: [[CONSTANT:%.+]] = cir.const #cir.int<16>
// CIR: [[ADDED:%.+]] = cir.binop(add, [[FP_OFFSET]], [[CONSTANT]])
// CIR: cir.store{{.*}} [[ADDED]], [[FP_OFFSET_P]]
// CIR: cir.br ^[[ContBlock:.+]]([[UPDATED]]
//
// CIR: ^[[InMemBlock]]:
// CIR: [[OVERFLOW_ARG_AREA_P:%.+]] = cir.get_member [[VASTED_VA_LIST]][2] {name = "overflow_arg_area"}
// CIR: [[OVERFLOW_ARG_AREA:%.+]] = cir.load [[OVERFLOW_ARG_AREA_P]]
// CIR: [[OFFSET:%.+]] = cir.const #cir.int<8>
// CIR: [[CASTED:%.+]] = cir.cast bitcast [[OVERFLOW_ARG_AREA]] : !cir.ptr<!void>
// CIR: [[NEW_VALUE:%.+]] = cir.ptr_stride inbounds [[CASTED]], [[OFFSET]] : (!cir.ptr<!s8i>, !s32i) -> !cir.ptr<!s8i>
// CIR: [[CASTED_BACK:%.+]] = cir.cast bitcast [[NEW_VALUE]] : !cir.ptr<!s8i> -> !cir.ptr<!void>
// CIR: cir.store [[CASTED_BACK]], [[OVERFLOW_ARG_AREA_P]]
// CIR: cir.br ^[[ContBlock]]([[OVERFLOW_ARG_AREA]]
//
// CIR: ^[[ContBlock]]([[ARG:.+]]: !cir.ptr
// CIR: [[CASTED_ARG_P:%.+]] = cir.cast bitcast [[ARG]]
// CIR: [[CASTED_ARG:%.+]] = cir.load align(8) [[CASTED_ARG_P]]
// CIR: cir.yield [[CASTED_ARG]]
//
// CIR: cir.store{{.*}} [[VAARG_RESULT]], [[RES]]

long double f2(int n, ...) {
  va_list valist;
  va_start(valist, n);
  long double res = va_arg(valist, long double);
  va_end(valist);
  return res;
}

// CHECK-LABEL: define {{.*}}@f2
// CHECK-NEXT: [[ARGN:%.+]] = alloca i32
// CHECK-NEXT: [[VA_LIST_ALLOCA:%.+]] = alloca {{.*}}[[VA_LIST_TYPE]]
// CHECK-NEXT: store i32 %0, ptr [[ARGN]]
// CHECK-NEXT: [[VA_LIST:%.+]] = getelementptr {{.*}} [[VA_LIST_ALLOCA]], i32 0
// CHECK-NEXT: call {{.*}}@llvm.va_start.p0(ptr [[VA_LIST]])
// CHECK-NEXT: [[VA_LIST2:%.+]] = getelementptr {{.*}} [[VA_LIST_ALLOCA]], i32 0
// CHECK-NEXT: [[OVERFLOW_ARG_AREA_P:%.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr [[VA_LIST2]], i32 0, i32 2
// CHECK-NEXT: [[OVERFLOW_ARG_AREA:%.+]] = load ptr, ptr [[OVERFLOW_ARG_AREA_P]]
// CHECK-NEXT: [[ROUND_UP:%.+]] = getelementptr inbounds{{( nuw)?}} i8, ptr [[OVERFLOW_ARG_AREA]], i64 15
// CHECK-NEXT: [[ALIGNED_PTR:%.+]] = {{(tail )?}}call{{( align 16)?}} ptr @llvm.ptrmask.p0.i64(ptr {{(nonnull )?}}[[ROUND_UP]], i64 -16)
// CHECK-NEXT: [[NEW_AREA:%.+]] = getelementptr inbounds i8, ptr [[ALIGNED_PTR]], i64 16
// CHECK-NEXT: store ptr [[NEW_AREA]], ptr [[OVERFLOW_ARG_AREA_P]]
// CHECK-NEXT: [[VA_ARG:%.+]] = load x86_fp80, ptr [[ALIGNED_PTR]]
// CHECK-NEXT: [[VA_LIST3:%.+]] = getelementptr {{.*}} [[VA_LIST_ALLOCA]], i32 0
// CHECK-NEXT: call {{.*}}@llvm.va_end.p0(ptr [[VA_LIST3]])
// CHECK-NEXT: ret x86_fp80 [[VA_ARG]]

// Long double (x86_fp80) is now properly lowered with 16-byte alignment
// via the overflow area, matching the AMD64 ABI.
// CIR: cir.func {{.*}} @f2
// CIR: [[VA_LIST_ALLOCA:%.+]] = cir.alloca !cir.array<!rec___va_list_tag x 1>, !cir.ptr<!cir.array<!rec___va_list_tag x 1>>, ["valist"
// CIR: [[RES:%.+]] = cir.alloca !cir.long_double<!cir.f80>, !cir.ptr<!cir.long_double<!cir.f80>>, ["res"
// CIR: cir.va_start
// CIR: [[OVERFLOW_P:%.+]] = cir.get_member {{%.+}}[2] {name = "overflow_arg_area"}
// CIR: [[OVERFLOW:%.+]] = cir.load [[OVERFLOW_P]]
// CIR: [[FIFTEEN:%.+]] = cir.const #cir.int<15> : !u64i
// CIR: [[OVERFLOW_I8:%.+]] = cir.cast bitcast [[OVERFLOW]] : !cir.ptr<!void> -> !cir.ptr<i8>
// CIR: [[ROUND_UP:%.+]] = cir.ptr_stride inbounds [[OVERFLOW_I8]], [[FIFTEEN]] : (!cir.ptr<i8>, !u64i) -> !cir.ptr<i8>
// CIR: [[ROUND_UP_VOID:%.+]] = cir.cast bitcast [[ROUND_UP]] : !cir.ptr<i8> -> !cir.ptr<!void>
// CIR: [[MASK:%.+]] = cir.const #cir.int<-16> : !s64i
// CIR: [[ALIGNED_PTR:%.+]] = cir.ptr_mask [[ROUND_UP_VOID]], [[MASK]] : (!cir.ptr<!void>, !s64i) -> !cir.ptr<!void>
// CIR: [[STEP:%.+]] = cir.const #cir.int<16> : !s32i
// CIR: [[CASTED:%.+]] = cir.cast bitcast [[ALIGNED_PTR]] : !cir.ptr<!void> -> !cir.ptr<!s8i>
// CIR: [[NEW_AREA:%.+]] = cir.ptr_stride inbounds [[CASTED]], [[STEP]] : (!cir.ptr<!s8i>, !s32i) -> !cir.ptr<!s8i>
// CIR: [[NEW_AREA_VOID:%.+]] = cir.cast bitcast [[NEW_AREA]] : !cir.ptr<!s8i> -> !cir.ptr<!void>
// CIR: cir.store [[NEW_AREA_VOID]], [[OVERFLOW_P]]
// CIR: [[LD_PTR:%.+]] = cir.cast bitcast [[ALIGNED_PTR]] : !cir.ptr<!void> -> !cir.ptr<!cir.long_double<!cir.f80>>
// CIR: [[VAARG_RESULT:%.+]] = cir.load [[LD_PTR]] : !cir.ptr<!cir.long_double<!cir.f80>>, !cir.long_double<!cir.f80>
// CIR: cir.store{{.*}} [[VAARG_RESULT]], [[RES]]

const char *f3(va_list args) {
  return va_arg(args, const char *);
}

// CHECK: define{{.*}} @f3
// CHECK: [[VA_LIST_ALLOCA:%.+]] = alloca ptr
// ...
// CHECK: [[VA_LIST:%.+]] = load {{.*}} [[VA_LIST_ALLOCA]]
// CHECK: [[OFFSET_PTR:%.+]] = getelementptr inbounds nuw {{.*}} [[VA_LIST]], i32 0, i32 0
// CHECK: [[OFFSET:%.+]] = load {{.*}}, ptr [[OFFSET_PTR]]
// CHECK: [[CMP:%.+]] = icmp ule i32 [[OFFSET]], 40
// CHECK: br i1 [[CMP]], label %[[THEN_BB:.+]], label %[[ELSE_BB:.+]]
//
// CHECK: [[THEN_BB]]:
// ...
// CHECK:   [[NEW_OFFSET:%.+]] = add i32 [[OFFSET]], 8
// CHECK:   store i32 [[NEW_OFFSET]], ptr [[OFFSET_PTR]]
// CHECK:   br label %[[CONT_BB:.+]]
//
// CHECK: [[ELSE_BB]]:
// ...
// CHECK:   [[OVERFLOW_ARG_AREA_ADDR:%.+]] = getelementptr inbounds nuw {{.*}} [[VA_LIST]], i32 0, i32 2
// CHECK:   [[OVERFLOW_ARG_AREA:%.+]] = load ptr, ptr [[OVERFLOW_ARG_AREA_ADDR]]
// CHECK:   [[OVERFLOW_ARG_AREA_OFFSET:%.+]] = getelementptr inbounds {{.*}} [[OVERFLOW_ARG_AREA]], i64 8
// CHECK:   store ptr [[OVERFLOW_ARG_AREA_OFFSET]], ptr [[OVERFLOW_ARG_AREA_ADDR]]
// CHECK:   br label %[[CONT_BB]]
//
// CHECK: [[CONT_BB]]:
// ...
// CHECK: ret

// CIR-LABEL:   cir.func {{.*}} @f3(
// CIR:           %[[VALIST_VAR:.*]] = cir.alloca !cir.ptr<!rec___va_list_tag>, !cir.ptr<!cir.ptr<!rec___va_list_tag>>, ["args", init] {alignment = 8 : i64}
// CIR:           %[[VALIST:.*]] = cir.load align(8) %[[VALIST_VAR]] : !cir.ptr<!cir.ptr<!rec___va_list_tag>>, !cir.ptr<!rec___va_list_tag>
// CIR:           %[[GP_OFFSET_PTR:.*]] = cir.get_member %[[VALIST]][0] {name = "gp_offset"} : !cir.ptr<!rec___va_list_tag> -> !cir.ptr<!u32i>
// CIR:           %[[GP_OFFSET:.*]] = cir.load align(8) %[[GP_OFFSET_PTR]] : !cir.ptr<!u32i>, !u32i
// CIR:           %[[VAL_6:.*]] = cir.const #cir.int<40> : !u32i
// CIR:           %[[VAL_7:.*]] = cir.cmp(le, %[[GP_OFFSET]], %[[VAL_6]]) : !u32i, !cir.bool
// CIR:           cir.brcond %[[VAL_7]]

// CIR:           %[[REG_SAVE_AREA_PTR:.*]] = cir.get_member %[[VALIST]][3] {name = "reg_save_area"} : !cir.ptr<!rec___va_list_tag> -> !cir.ptr<!cir.ptr<!void>>
// CIR:           %[[REG_SAVE_AREA:.*]] = cir.load align(8) %[[REG_SAVE_AREA_PTR]] : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
// CIR:           %[[CUR_REG_SAVE_AREA:.*]] = cir.ptr_stride inbounds %[[REG_SAVE_AREA]], %[[GP_OFFSET]] : (!cir.ptr<!void>, !u32i) -> !cir.ptr<!void>
// CIR:           %[[VAL_11:.*]] = cir.const #cir.int<8> : !u32i
// CIR:           %[[NEW_REG_SAVE_AREA:.*]] = cir.binop(add, %[[GP_OFFSET]], %[[VAL_11]]) : !u32i
// CIR:           cir.store align(8) %[[NEW_REG_SAVE_AREA]], %[[GP_OFFSET_PTR]] : !u32i, !cir.ptr<!u32i>
// CIR:           cir.br ^[[CONT_BB:.*]](%[[CUR_REG_SAVE_AREA]] : !cir.ptr<!void>)

// CIR:           %[[OVERFLOW_ARG_AREA_PTR:.*]] = cir.get_member %[[VALIST]][2] {name = "overflow_arg_area"} : !cir.ptr<!rec___va_list_tag> -> !cir.ptr<!cir.ptr<!void>>
// CIR:           %[[OVERFLOW_ARG_AREA:.*]] = cir.load %[[OVERFLOW_ARG_AREA_PTR]] : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
// CIR:           %[[VAL_15:.*]] = cir.const #cir.int<8> : !s32i
// CIR:           %[[CUR_OVERFLOW_ARG_AREA:.*]] = cir.cast bitcast %[[OVERFLOW_ARG_AREA]] : !cir.ptr<!void> -> !cir.ptr<!s8i>
// CIR:           %[[NEW_OVERFLOW_ARG_AREA:.*]] = cir.ptr_stride inbounds %[[CUR_OVERFLOW_ARG_AREA]], %[[VAL_15]] : (!cir.ptr<!s8i>, !s32i) -> !cir.ptr<!s8i>
// CIR:           %[[CASTED_BACK:.*]] = cir.cast bitcast %[[NEW_OVERFLOW_ARG_AREA]] : !cir.ptr<!s8i> -> !cir.ptr<!void>
// CIR:           cir.store %[[CASTED_BACK]], %[[OVERFLOW_ARG_AREA_PTR]] : !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>
// CIR:           cir.br ^[[CONT_BB]](%[[OVERFLOW_ARG_AREA]] : !cir.ptr<!void>)

// ...
// CIR:           cir.return

void f4(va_list args) {
  for (; va_arg(args, int); );
}
// CIR-LABEL:   cir.func {{.*}} @f4
// CIR:           cir.for : cond {
// CIR:             %[[VALIST:.*]] = cir.load align(8) %{{.*}} : !cir.ptr<!cir.ptr<!rec___va_list_tag>>, !cir.ptr<!rec___va_list_tag>
// CIR:             %[[VAARG_RESULT:.*]] = cir.scope {
//                    ... // The contents are tested elsewhere.
// CIR:               cir.yield {{.*}} : !s32i
// CIR:             } : !s32i
// CIR:             %[[CMP:.*]] = cir.cast int_to_bool %[[VAARG_RESULT]] : !s32i -> !cir.bool
// CIR:             cir.condition(%[[CMP]])
// CIR:           } body {
// CIR:             cir.yield
// CIR:           } step {
// CIR:             cir.yield
// CIR:           }
// CIR:           cir.return
// CIR:         }

_Complex float f5(int n, ...) {
  va_list valist;
  va_start(valist, n);
  _Complex float res = va_arg(valist, _Complex float);
  va_end(valist);
  return res;
}

// CHECK: define {{.*}}@f5
// CHECK: [[F5_FP_OFFSET_P:%.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr {{.*}}, i32 0, i32 1
// CHECK: [[F5_FP_OFFSET:%.+]] = load i32, ptr [[F5_FP_OFFSET_P]]
// CHECK: [[F5_INREG:%.+]] = icmp ule i32 [[F5_FP_OFFSET]], 160
// CHECK: br i1 [[F5_INREG]], label %[[F5_INREG_BB:.+]], label %[[F5_INMEM_BB:.+]]
// CHECK: [[F5_INREG_BB]]:
// CHECK:   [[F5_REG_SAVE:%.+]] = load ptr, ptr {{.*}}
// CHECK:   [[F5_REG_ADDR:%.+]] = getelementptr inbounds i8, ptr [[F5_REG_SAVE]], i64
// CHECK:   [[F5_UPDATED_FP:%.+]] = add i32 [[F5_FP_OFFSET]], 16
// CHECK:   store i32 [[F5_UPDATED_FP]], ptr [[F5_FP_OFFSET_P]]
// CHECK:   br label %[[F5_CONT:.+]]
// CHECK: [[F5_INMEM_BB]]:
// CHECK:   [[F5_OVERFLOW_P:%.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr {{.*}}, i32 0, i32 2
// CHECK:   [[F5_OVERFLOW:%.+]] = load ptr, ptr [[F5_OVERFLOW_P]]
// CHECK:   [[F5_NEXT_OVERFLOW:%.+]] = getelementptr inbounds i8, ptr [[F5_OVERFLOW]], i64 8
// CHECK:   store ptr [[F5_NEXT_OVERFLOW]], ptr [[F5_OVERFLOW_P]]
// CHECK:   br label %[[F5_CONT]]
// CHECK: [[F5_CONT]]:
// CHECK:   [[F5_ADDR:%.+]] = phi ptr [ [[F5_OVERFLOW]], %[[F5_INMEM_BB]] ], [ [[F5_REG_ADDR]], %[[F5_INREG_BB]] ]
// CHECK:   [[F5_VAL:%.+]] = load { float, float }, ptr [[F5_ADDR]], align 4
// CHECK-NOT: va_arg ptr

// CIR-LABEL:   cir.func {{.*}} @f5
// CIR:           %[[F5_VALIST_START:.*]] = cir.cast array_to_ptrdecay
// CIR:           cir.va_start %[[F5_VALIST_START]] %{{.*}} : !cir.ptr<!rec___va_list_tag>, !s32i
// CIR:           %[[F5_VALIST:.*]] = cir.cast array_to_ptrdecay
// CIR:           %[[F5_RESULT:.*]] = cir.scope {
// CIR:             %[[F5_FP_OFFSET_P:.*]] = cir.get_member %[[F5_VALIST]][1] {name = "fp_offset"}
// CIR:             %[[F5_FP_OFFSET:.*]] = cir.load align(4) %[[F5_FP_OFFSET_P]]
// CIR:             %[[F5_MAX:.*]] = cir.const #cir.int<160> : !u32i
// CIR:             %[[F5_CMP:.*]] = cir.cmp(le, %[[F5_FP_OFFSET]], %[[F5_MAX]])
// CIR:             cir.brcond %[[F5_CMP]]
// CIR:           ^{{.+}}:
// CIR:             %[[F5_REG_SAVE:.*]] = cir.get_member %[[F5_VALIST]][3] {name = "reg_save_area"}
// CIR:             %[[F5_REG_ADDR:.*]] = cir.ptr_stride inbounds %{{.*}}, %[[F5_FP_OFFSET]]
// CIR:             %[[F5_STEP:.*]] = cir.const #cir.int<16> : !u32i
// CIR:             %[[F5_UPDATED:.*]] = cir.binop(add, %[[F5_FP_OFFSET]], %[[F5_STEP]]) : !u32i
// CIR:             cir.store align(4) %[[F5_UPDATED]], %[[F5_FP_OFFSET_P]]
// CIR:             cir.br ^{{.+}}(%[[F5_REG_ADDR]] : !cir.ptr<!void>)
// CIR:           ^{{.+}}:
// CIR:             %[[F5_OVERFLOW_P:.*]] = cir.get_member %[[F5_VALIST]][2] {name = "overflow_arg_area"}
// CIR:             %[[F5_OVERFLOW:.*]] = cir.load %[[F5_OVERFLOW_P]]
// CIR:             %[[F5_MEM_STEP:.*]] = cir.const #cir.int<8> : !s32i
// CIR:             %[[F5_NEXT_OVERFLOW:.*]] = cir.ptr_stride inbounds %{{.*}}, %[[F5_MEM_STEP]]
// CIR:             cir.store %{{.*}}, %[[F5_OVERFLOW_P]]
// CIR:             cir.br ^{{.+}}(%[[F5_OVERFLOW]] : !cir.ptr<!void>)
// CIR:           ^{{.+}}(%[[F5_ARG:.*]]: !cir.ptr<!void>
// CIR:             %[[F5_TYPED:.*]] = cir.cast bitcast %[[F5_ARG]] : !cir.ptr<!void> -> !cir.ptr<!cir.complex<!cir.float>>
// CIR:             %[[F5_LOADED:.*]] = cir.load align(4) %[[F5_TYPED]] : !cir.ptr<!cir.complex<!cir.float>>, !cir.complex<!cir.float>
// CIR:             cir.yield %[[F5_LOADED]] : !cir.complex<!cir.float>

_Complex double f6(int n, ...) {
  va_list valist;
  va_start(valist, n);
  _Complex double res = va_arg(valist, _Complex double);
  va_end(valist);
  return res;
}

// CHECK: define {{.*}}@f6
// CHECK: [[F6_FP_OFFSET_P:%.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr {{.*}}, i32 0, i32 1
// CHECK: [[F6_FP_OFFSET:%.+]] = load i32, ptr [[F6_FP_OFFSET_P]]
// CHECK: [[F6_INREG:%.+]] = icmp ule i32 [[F6_FP_OFFSET]], 144
// CHECK: br i1 [[F6_INREG]], label %[[F6_INREG_BB:.+]], label %[[F6_INMEM_BB:.+]]
// CHECK: [[F6_INREG_BB]]:
// CHECK:   [[F6_REG_SAVE:%.+]] = load ptr, ptr {{.*}}
// CHECK:   [[F6_REG_ADDR_LO:%.+]] = getelementptr inbounds i8, ptr [[F6_REG_SAVE]], i64
// CHECK:   [[F6_REG_ADDR_HI:%.+]] = getelementptr inbounds i8, ptr [[F6_REG_ADDR_LO]], i64 16
// CHECK:   [[F6_LO:%.+]] = load double, ptr [[F6_REG_ADDR_LO]], align 16
// CHECK:   [[F6_HI:%.+]] = load double, ptr [[F6_REG_ADDR_HI]], align 16
// CHECK:   [[F6_UPDATED_FP:%.+]] = add i32 [[F6_FP_OFFSET]], 32
// CHECK:   store i32 [[F6_UPDATED_FP]], ptr [[F6_FP_OFFSET_P]]
// CHECK:   br label %[[F6_CONT:.+]]
// CHECK: [[F6_INMEM_BB]]:
// CHECK:   [[F6_OVERFLOW_P:%.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr {{.*}}, i32 0, i32 2
// CHECK:   [[F6_OVERFLOW:%.+]] = load ptr, ptr [[F6_OVERFLOW_P]]
// CHECK:   [[F6_NEXT_OVERFLOW:%.+]] = getelementptr inbounds i8, ptr [[F6_OVERFLOW]], i64 16
// CHECK:   store ptr [[F6_NEXT_OVERFLOW]], ptr [[F6_OVERFLOW_P]]
// CHECK:   br label %[[F6_CONT]]
// CHECK: [[F6_CONT]]:
// CHECK:   [[F6_ADDR:%.+]] = phi ptr [ [[F6_OVERFLOW]], %[[F6_INMEM_BB]] ], [ {{.*}}, %[[F6_INREG_BB]] ]
// CHECK:   [[F6_VAL:%.+]] = load { double, double }, ptr [[F6_ADDR]], align 8
// CHECK-NOT: va_arg ptr

// CIR-LABEL:   cir.func {{.*}} @f6
// CIR:           %[[F6_VALIST_START:.*]] = cir.cast array_to_ptrdecay
// CIR:           cir.va_start %[[F6_VALIST_START]] %{{.*}} : !cir.ptr<!rec___va_list_tag>, !s32i
// CIR:           %[[F6_VALIST:.*]] = cir.cast array_to_ptrdecay
// CIR:           %[[F6_RESULT:.*]] = cir.scope {
// CIR:             %[[F6_FP_OFFSET_P:.*]] = cir.get_member %[[F6_VALIST]][1] {name = "fp_offset"}
// CIR:             %[[F6_FP_OFFSET:.*]] = cir.load align(4) %[[F6_FP_OFFSET_P]]
// CIR:             %[[F6_MAX:.*]] = cir.const #cir.int<144> : !u32i
// CIR:             %[[F6_CMP:.*]] = cir.cmp(le, %[[F6_FP_OFFSET]], %[[F6_MAX]])
// CIR:             cir.brcond %[[F6_CMP]]
// CIR:           ^{{.+}}:
// CIR:             %[[F6_REG_SAVE:.*]] = cir.get_member %[[F6_VALIST]][3] {name = "reg_save_area"}
// CIR:             %[[F6_REG_ADDR_LO:.*]] = cir.ptr_stride inbounds %{{.*}}, %[[F6_FP_OFFSET]]
// CIR:             %[[F6_SSE_STEP:.*]] = cir.const #cir.int<16> : !u32i
// CIR:             %[[F6_REG_ADDR_HI:.*]] = cir.ptr_stride inbounds %[[F6_REG_ADDR_LO]], %[[F6_SSE_STEP]]
// CIR:             %[[F6_TMP:.*]] = cir.alloca !cir.complex<!cir.double>
// CIR:             %[[F6_TMP_RECORD:.*]] = cir.cast bitcast %[[F6_TMP]] : !cir.ptr<!cir.complex<!cir.double>> -> !cir.ptr<!rec_anon_struct>
// CIR:             %[[F6_LO_PTR:.*]] = cir.cast bitcast %[[F6_REG_ADDR_LO]] : !cir.ptr<!void> -> !cir.ptr<!cir.double>
// CIR:             %[[F6_LO:.*]] = cir.load align(16) %[[F6_LO_PTR]]
// CIR:             cir.store align(8) %[[F6_LO]], %{{.*}} : !cir.double, !cir.ptr<!cir.double>
// CIR:             %[[F6_HI_PTR:.*]] = cir.cast bitcast %[[F6_REG_ADDR_HI]] : !cir.ptr<!void> -> !cir.ptr<!cir.double>
// CIR:             %[[F6_HI:.*]] = cir.load align(16) %[[F6_HI_PTR]]
// CIR:             cir.store align(8) %[[F6_HI]], %{{.*}} : !cir.double, !cir.ptr<!cir.double>
// CIR:             %[[F6_TYPED_TMP:.*]] = cir.cast bitcast %[[F6_TMP_RECORD]] : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!cir.complex<!cir.double>>
// CIR:             %[[F6_UPDATE_STEP:.*]] = cir.const #cir.int<32> : !u32i
// CIR:             %[[F6_UPDATED:.*]] = cir.binop(add, %[[F6_FP_OFFSET]], %[[F6_UPDATE_STEP]]) : !u32i
// CIR:             cir.store align(4) %[[F6_UPDATED]], %[[F6_FP_OFFSET_P]]
// CIR:             cir.br ^{{.+}}(%[[F6_TYPED_TMP]] : !cir.ptr<!cir.complex<!cir.double>>)
// CIR:           ^{{.+}}:
// CIR:             %[[F6_OVERFLOW_P:.*]] = cir.get_member %[[F6_VALIST]][2] {name = "overflow_arg_area"}
// CIR:             %[[F6_OVERFLOW:.*]] = cir.load %[[F6_OVERFLOW_P]]
// CIR:             %[[F6_MEM_STEP:.*]] = cir.const #cir.int<16> : !s32i
// CIR:             %[[F6_NEXT_OVERFLOW:.*]] = cir.ptr_stride inbounds %{{.*}}, %[[F6_MEM_STEP]]
// CIR:             cir.store %{{.*}}, %[[F6_OVERFLOW_P]]
// CIR:             %[[F6_TYPED_OVERFLOW:.*]] = cir.cast bitcast %[[F6_OVERFLOW]] : !cir.ptr<!void> -> !cir.ptr<!cir.complex<!cir.double>>
// CIR:             cir.br ^{{.+}}(%[[F6_TYPED_OVERFLOW]] : !cir.ptr<!cir.complex<!cir.double>>)
// CIR:           ^{{.+}}(%[[F6_ARG:.*]]: !cir.ptr<!cir.complex<!cir.double>>
// CIR:             %[[F6_LOADED:.*]] = cir.load %[[F6_ARG]] : !cir.ptr<!cir.complex<!cir.double>>, !cir.complex<!cir.double>
// CIR:             cir.yield %[[F6_LOADED]] : !cir.complex<!cir.double>

_Complex long double f7(int n, ...) {
  va_list valist;
  va_start(valist, n);
  _Complex long double res = va_arg(valist, _Complex long double);
  va_end(valist);
  return res;
}

// CHECK: define {{.*}}@f7
// CHECK: [[F7_OVERFLOW_P:%.+]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr {{.*}}, i32 0, i32 2
// CHECK: [[F7_OVERFLOW:%.+]] = load ptr, ptr [[F7_OVERFLOW_P]]
// CHECK: [[F7_ROUND_UP:%.+]] = getelementptr inbounds{{( nuw)?}} i8, ptr [[F7_OVERFLOW]], i64 15
// CHECK: [[F7_ALIGNED:%.+]] = {{(tail )?}}call{{( align 16)?}} ptr @llvm.ptrmask.p0.i64(ptr {{(nonnull )?}}[[F7_ROUND_UP]], i64 -16)
// CHECK: [[F7_NEXT_OVERFLOW:%.+]] = getelementptr inbounds i8, ptr [[F7_ALIGNED]], i64 32
// CHECK: store ptr [[F7_NEXT_OVERFLOW]], ptr [[F7_OVERFLOW_P]]
// CHECK: [[F7_VAL:%.+]] = load { x86_fp80, x86_fp80 }, ptr [[F7_ALIGNED]], align 16
// CHECK-NOT: va_arg ptr

// CIR-LABEL:   cir.func {{.*}} @f7
// CIR:           %[[F7_VALIST_START:.*]] = cir.cast array_to_ptrdecay
// CIR:           cir.va_start %[[F7_VALIST_START]] %{{.*}} : !cir.ptr<!rec___va_list_tag>, !s32i
// CIR:           %[[F7_VALIST:.*]] = cir.cast array_to_ptrdecay
// CIR:           %[[F7_OVERFLOW_P:.*]] = cir.get_member %[[F7_VALIST]][2] {name = "overflow_arg_area"}
// CIR:           %[[F7_OVERFLOW:.*]] = cir.load %[[F7_OVERFLOW_P]]
// CIR:           %[[F7_ADDEND:.*]] = cir.const #cir.int<15> : !u64i
// CIR:           %[[F7_OVERFLOW_I8:.*]] = cir.cast bitcast %[[F7_OVERFLOW]] : !cir.ptr<!void> -> !cir.ptr<i8>
// CIR:           %[[F7_ROUND_UP:.*]] = cir.ptr_stride inbounds %[[F7_OVERFLOW_I8]], %[[F7_ADDEND]] : (!cir.ptr<i8>, !u64i) -> !cir.ptr<i8>
// CIR:           %[[F7_ROUND_UP_VOID:.*]] = cir.cast bitcast %[[F7_ROUND_UP]] : !cir.ptr<i8> -> !cir.ptr<!void>
// CIR:           %[[F7_MASK:.*]] = cir.const #cir.int<-16> : !s64i
// CIR:           %[[F7_ALIGNED:.*]] = cir.ptr_mask %[[F7_ROUND_UP_VOID]], %[[F7_MASK]] : (!cir.ptr<!void>, !s64i) -> !cir.ptr<!void>
// CIR:           %[[F7_STEP:.*]] = cir.const #cir.int<32> : !s32i
// CIR:           %[[F7_NEXT_OVERFLOW:.*]] = cir.ptr_stride inbounds %{{.*}}, %[[F7_STEP]]
// CIR:           cir.store %{{.*}}, %[[F7_OVERFLOW_P]]
// CIR:           %[[F7_TYPED:.*]] = cir.cast bitcast %[[F7_ALIGNED]] : !cir.ptr<!void> -> !cir.ptr<!cir.complex<!cir.long_double<!cir.f80>>>
// CIR:           %[[F7_LOADED:.*]] = cir.load %[[F7_TYPED]] : !cir.ptr<!cir.complex<!cir.long_double<!cir.f80>>>, !cir.complex<!cir.long_double<!cir.f80>>
