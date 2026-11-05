// RUN: %clang_cc1 -triple x86_64-linux-gnu -gkey-instructions -gno-column-info \
// RUN:   -debug-info-kind=line-tables-only -emit-llvm -fclangir %s -o - \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -x c++ -std=c++17 \
// RUN:   -gkey-instructions -gno-column-info -debug-info-kind=line-tables-only \
// RUN:   -emit-llvm -fclangir -fexceptions -fcxx-exceptions %s -o - \
// RUN:   | FileCheck %s --check-prefix=CHECK-CXX

int g, h;

void if_assign(int A) {
  if (A)
    ;
  if ((g = A))
    ;
}

void while_dec(int A) {
  while (--A) {
  }
}

void while_rel(void) {
  while (g < 3)
    g = 4;
}

void for_rel(void) {
  for (; g < 3;)
    g = 5;
}

void do_rel(void) {
  do
    g = 6;
  while (g < 3);
}

int return_assign(void) {
  return g = 1;
}

float branch_return(void) {
  if (g)
    return g;
  return 1;
}

void switch_break(int A) {
  switch (g = A) {
  case 0:
    break;
  }
}

void compound_assign_outer_atoms(void) {
  g += 50;
  g += ++h;
}

void goto_branches(void) {
  goto dst1;
dst1:
  g = 1;

  void *ptr = &&dst2;
  goto *ptr;
dst2:
  g = 2;

  goto *&&dst3;
dst3:
  g = 3;
}

typedef struct { struct {} a; double b; } va_arg_s1;

va_arg_s1 return_vaarg(int z, ...) {
  __builtin_va_list list;
  __builtin_va_start(list, z);
  return __builtin_va_arg(list, va_arg_s1);
}

#ifdef __cplusplus
void except_value();

void if_cond_decl(int A) {
  if (int B = A; B)
    ;
}

void try_catch_value() {
  try {
    except_value();
  } catch (int e) {
  }
}
#endif

// CHECK-LABEL: define dso_local void @if_assign(
// CHECK: %[[IF_LOAD0:[0-9]+]] = load i32, ptr %{{.*}}, align 4, !dbg [[IF_LOAD0DBG:!.*]]
// CHECK: %[[IF_CMP0:[0-9]+]] = icmp ne i32 %[[IF_LOAD0]], 0, !dbg [[IF_G1R2:!.*]]
// CHECK: br i1 %[[IF_CMP0]], label %{{.*}}, label %{{.*}}, !dbg [[IF_G1R1:!.*]]
// CHECK: %[[IF_LOAD1:[0-9]+]] = load i32, ptr %{{.*}}, align 4, !dbg [[IF_G2R2:!.*]]
// CHECK: store i32 %[[IF_LOAD1]], ptr @g, align 4, !dbg [[IF_G2R1:!.*]]
// CHECK: %[[IF_CMP1:[0-9]+]] = icmp ne i32 %[[IF_LOAD1]], 0, !dbg [[IF_G3R2:!.*]]
// CHECK: br i1 %[[IF_CMP1]], label %{{.*}}, label %{{.*}}, !dbg [[IF_G3R1:!.*]]
// CHECK: ret void, !dbg [[IF_RET:!.*]]

// CHECK-LABEL: define dso_local void @while_dec(
// CHECK: %[[WHILE_LOAD:[0-9]+]] = load i32, ptr %{{.*}}, align 4, !dbg [[WHILE_LOADDBG:!.*]]
// CHECK: %[[WHILE_DEC:[0-9]+]] = sub nsw i32 %[[WHILE_LOAD]], 1, !dbg [[WHILE_G1R2:!.*]]
// CHECK: store i32 %[[WHILE_DEC]], ptr %{{.*}}, align 4, !dbg [[WHILE_G1R1:!.*]]
// CHECK: %[[WHILE_CMP:[0-9]+]] = icmp ne i32 %[[WHILE_DEC]], 0, !dbg [[WHILE_G2R1:!.*]]
// CHECK: br i1 %[[WHILE_CMP]], label %{{.*}}, label %{{.*}}, !dbg [[WHILE_G3R1:!.*]]
// CHECK: ret void, !dbg [[WHILE_RET:!.*]]

// CHECK-LABEL: define dso_local void @while_rel(
// CHECK: %[[WHILE_REL_LOAD:[0-9]+]] = load i32, ptr @g, align 4, !dbg [[WHILE_REL_LOADDBG:!.*]]
// CHECK: %[[WHILE_REL_CMP:[0-9]+]] = icmp slt i32 %[[WHILE_REL_LOAD]], 3, !dbg [[WHILE_REL_G1R1:!.*]]
// CHECK: br i1 %[[WHILE_REL_CMP]], label %{{.*}}, label %{{.*}}, !dbg [[WHILE_REL_G2R1:!.*]]
// CHECK: store i32 4, ptr @g, align 4, !dbg [[WHILE_REL_G3R1:!.*]]
// CHECK: ret void, !dbg [[WHILE_REL_RET:!.*]]

// CHECK-LABEL: define dso_local void @for_rel(
// CHECK: %[[FOR_REL_LOAD:[0-9]+]] = load i32, ptr @g, align 4, !dbg [[FOR_REL_LOADDBG:!.*]]
// CHECK: %[[FOR_REL_CMP:[0-9]+]] = icmp slt i32 %[[FOR_REL_LOAD]], 3, !dbg [[FOR_REL_G1R1:!.*]]
// CHECK: br i1 %[[FOR_REL_CMP]], label %{{.*}}, label %{{.*}}, !dbg [[FOR_REL_G2R1:!.*]]
// CHECK: store i32 5, ptr @g, align 4, !dbg [[FOR_REL_G3R1:!.*]]
// CHECK: br label %{{.*}}, !dbg [[FOR_REL_G4R1:!.*]], !llvm.loop ![[#]]
// CHECK: ret void, !dbg [[FOR_REL_RET:!.*]]

// CHECK-LABEL: define dso_local void @do_rel(
// CHECK: %[[DO_REL_LOAD:[0-9]+]] = load i32, ptr @g, align 4, !dbg [[DO_REL_LOADDBG:!.*]]
// CHECK: %[[DO_REL_CMP:[0-9]+]] = icmp slt i32 %[[DO_REL_LOAD]], 3, !dbg [[DO_REL_G2R1:!.*]]
// CHECK: br i1 %[[DO_REL_CMP]], label %{{.*}}, label %{{.*}}, !dbg [[DO_REL_G3R1:![0-9]+]], !llvm.loop ![[#]]
// CHECK: store i32 6, ptr @g, align 4, !dbg [[DO_REL_G1R1:!.*]]
// CHECK: ret void, !dbg [[DO_REL_RET:!.*]]

// CHECK-LABEL: define dso_local i32 @return_assign(
// CHECK: store i32 1, ptr @g, align 4, !dbg [[RET_ASSIGN_G2R1:!.*]]
// CHECK: ret i32 1, !dbg [[RET_ASSIGN_G1R1:!.*]]

// CHECK-LABEL: define dso_local float @branch_return(
// CHECK: %[[RETCOND_LOAD:[0-9]+]] = load i32, ptr @g, align 4, !dbg [[RETCOND_LOADDBG:!.*]]
// CHECK: %[[RETCOND_CMP:[0-9]+]] = icmp ne i32 %[[RETCOND_LOAD]], 0, !dbg [[RETCOND_G1R2:!.*]]
// CHECK: br i1 %[[RETCOND_CMP]], label %{{.*}}, label %{{.*}}, !dbg [[RETCOND_G1R1:!.*]]
// CHECK: %[[RETVAL_LOAD:[0-9]+]] = load i32, ptr @g, align 4, !dbg [[RETTHEN_G2R3:!.*]]
// CHECK: %[[RETVAL_CONV:[0-9]+]] = sitofp i32 %[[RETVAL_LOAD]] to float, !dbg [[RETTHEN_G2R2:!.*]]
// CHECK: store float %[[RETVAL_CONV]], ptr %{{.*}}, align 4, !dbg [[RETTHEN_G2R1:!.*]]
// CHECK: br label %{{.*}}, !dbg [[RETTHEN_G2R1]]
// CHECK: store float 1.000000e+00, ptr %{{.*}}, align 4, !dbg [[RETELSE_G3R1:!.*]]
// CHECK: br label %{{.*}}, !dbg [[RETELSE_G3R1]]
// CHECK: %[[RETLOAD:[0-9]+]] = load float, ptr %{{.*}}, align 4, !dbg [[RET_G4R2:!.*]]
// CHECK: ret float %[[RETLOAD]], !dbg [[RET_G4R1:!.*]]

// CHECK-LABEL: define dso_local void @switch_break(
// CHECK: %[[SWITCH_LOAD:[0-9]+]] = load i32, ptr %{{.*}}, align 4, !dbg [[SWITCH_G2R2:!.*]]
// CHECK: store i32 %[[SWITCH_LOAD]], ptr @g, align 4, !dbg [[SWITCH_G1R1:!.*]]
// CHECK: switch i32 %[[SWITCH_LOAD]], label %{{.*}} [
// CHECK:   i32 0, label %{{.*}}
// CHECK: ], !dbg [[SWITCH_G2R1:!.*]]
// CHECK: br label %{{.*}}, !dbg [[SWITCH_G3R1:!.*]]
// CHECK: ret void, !dbg [[SWITCH_RET:!.*]]

// CHECK-LABEL: define dso_local void @compound_assign_outer_atoms(
// CHECK: %[[CMP_ASSIGN_LOAD0:[0-9]+]] = load i32, ptr @g, align 4, !dbg [[CMP_ASSIGN_LOAD0DBG:!.*]]
// CHECK: %[[CMP_ASSIGN_ADD0:[0-9]+]] = add nsw i32 %[[CMP_ASSIGN_LOAD0]], 50, !dbg [[CMP_ASSIGN_G1R2:!.*]]
// CHECK: store i32 %[[CMP_ASSIGN_ADD0]], ptr @g, align 4, !dbg [[CMP_ASSIGN_G1R1:!.*]]
// CHECK: %[[CMP_ASSIGN_HLOAD:[0-9]+]] = load i32, ptr @h, align 4, !dbg [[CMP_ASSIGN_HLOADDBG:!.*]]
// CHECK: %[[CMP_ASSIGN_HINC:[0-9]+]] = add nsw i32 %[[CMP_ASSIGN_HLOAD]], 1, !dbg [[CMP_ASSIGN_G3R2:!.*]]
// CHECK: store i32 %[[CMP_ASSIGN_HINC]], ptr @h, align 4, !dbg [[CMP_ASSIGN_G3R1:!.*]]
// CHECK: %[[CMP_ASSIGN_GLOAD1:[0-9]+]] = load i32, ptr @g, align 4, !dbg [[CMP_ASSIGN_HLOADDBG]]
// CHECK: %[[CMP_ASSIGN_ADD1:[0-9]+]] = add nsw i32 %[[CMP_ASSIGN_GLOAD1]], %[[CMP_ASSIGN_HINC]], !dbg [[CMP_ASSIGN_G2R2:!.*]]
// CHECK: store i32 %[[CMP_ASSIGN_ADD1]], ptr @g, align 4, !dbg [[CMP_ASSIGN_G2R1:!.*]]
// CHECK: ret void, !dbg [[CMP_ASSIGN_RET:!.*]]

// CHECK-LABEL: define dso_local void @goto_branches(
// CHECK: br label %{{.*}}, !dbg [[GOTO_G1R1:!.*]]
// CHECK: store i32 1, ptr @g, align 4, !dbg [[GOTO_STORE1:!.*]]
// CHECK: store ptr blockaddress(@goto_branches, %{{.*}}), ptr %{{.*}}, align 8, !dbg [[GOTO_G2R1:!.*]]
// CHECK: br label %{{.*}}, !dbg [[GOTO_G3R1:!.*]]
// CHECK: indirectbr ptr %{{.*}}, [label %{{.*}}]
// CHECK: store i32 2, ptr @g, align 4, !dbg [[GOTO_STORE2:!.*]]
// CHECK: br label %{{.*}}, !dbg [[GOTO_G4R1:!.*]]
// CHECK: store i32 3, ptr @g, align 4, !dbg [[GOTO_STORE3:!.*]]
// CHECK: ret void, !dbg [[GOTO_RET:!.*]]

// CHECK-LABEL: define dso_local double @return_vaarg(
// CHECK: call void @llvm.memcpy{{.*}}, !dbg [[VAARG_G1R1:!.*]]
// CHECK: %[[VAARG_LOAD:[0-9]+]] = load double, ptr %{{.*}}, align 8, !dbg [[VAARG_G1R2:!.*]]
// CHECK: ret double %[[VAARG_LOAD]], !dbg [[VAARG_G1R1]]

// CHECK: [[IF_LOAD0DBG]] = !DILocation(line: 12, scope: ![[#]])
// CHECK: [[IF_G1R2]] = !DILocation({{.*}}atomGroup: 1, atomRank: 2)
// CHECK: [[IF_G1R1]] = !DILocation({{.*}}atomGroup: 1, atomRank: 1)
// CHECK: [[IF_G2R2]] = !DILocation({{.*}}atomGroup: 2, atomRank: 2)
// CHECK: [[IF_G2R1]] = !DILocation({{.*}}atomGroup: 2, atomRank: 1)
// CHECK: [[IF_G3R2]] = !DILocation({{.*}}atomGroup: 3, atomRank: 2)
// CHECK: [[IF_G3R1]] = !DILocation({{.*}}atomGroup: 3, atomRank: 1)
// CHECK: [[IF_RET]] = !DILocation({{.*}}atomGroup: 4, atomRank: 1)

// CHECK: [[WHILE_G1R2]] = !DILocation({{.*}}atomGroup: 1, atomRank: 2)
// CHECK: [[WHILE_G1R1]] = !DILocation({{.*}}atomGroup: 1, atomRank: 1)
// CHECK: [[WHILE_G2R1]] = !DILocation({{.*}}atomGroup: 2, atomRank: 1)
// CHECK: [[WHILE_G3R1]] = !DILocation({{.*}}atomGroup: 3, atomRank: 1)
// CHECK: [[WHILE_RET]] = !DILocation({{.*}}atomGroup: 4, atomRank: 1)

// CHECK: [[WHILE_REL_LOADDBG]] = !DILocation(line: 24, scope: ![[#]])
// CHECK: [[WHILE_REL_G1R1]] = !DILocation({{.*}}atomGroup: 1, atomRank: 1)
// CHECK: [[WHILE_REL_G2R1]] = !DILocation({{.*}}atomGroup: 2, atomRank: 1)
// CHECK: [[WHILE_REL_G3R1]] = !DILocation({{.*}}atomGroup: 3, atomRank: 1)
// CHECK: [[WHILE_REL_RET]] = !DILocation({{.*}}atomGroup: 4, atomRank: 1)

// CHECK: [[FOR_REL_LOADDBG]] = !DILocation(line: 29, scope: ![[#]])
// CHECK: [[FOR_REL_G1R1]] = !DILocation({{.*}}atomGroup: 1, atomRank: 1)
// CHECK: [[FOR_REL_G2R1]] = !DILocation({{.*}}atomGroup: 2, atomRank: 1)
// CHECK: [[FOR_REL_G3R1]] = !DILocation({{.*}}atomGroup: 3, atomRank: 1)
// CHECK: [[FOR_REL_G4R1]] = !DILocation(line: 29, scope: ![[#]], atomGroup: 4, atomRank: 1)
// CHECK: [[FOR_REL_RET]] = !DILocation(line: 31, scope: ![[#]], atomGroup: 5, atomRank: 1)

// CHECK: [[DO_REL_LOADDBG]] = !DILocation(line: 36, scope: ![[#]])
// CHECK: [[DO_REL_G2R1]] = !DILocation(line: 36, scope: ![[#]], atomGroup: 2, atomRank: 1)
// CHECK: [[DO_REL_G3R1]] = !DILocation(line: 35, scope: ![[#]], atomGroup: 3, atomRank: 1)
// CHECK: [[DO_REL_G1R1]] = !DILocation(line: 35, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK: [[DO_REL_RET]] = !DILocation(line: 37, scope: ![[#]], atomGroup: 4, atomRank: 1)

// CHECK: [[RET_ASSIGN_G2R1]] = !DILocation({{.*}}atomGroup: 2, atomRank: 1)
// CHECK: [[RET_ASSIGN_G1R1]] = !DILocation({{.*}}atomGroup: 1, atomRank: 1)

// CHECK: [[RETCOND_LOADDBG]] = !DILocation(line: 44, scope: ![[#]])
// CHECK: [[RETCOND_G1R2]] = !DILocation({{.*}}atomGroup: 1, atomRank: 2)
// CHECK: [[RETCOND_G1R1]] = !DILocation({{.*}}atomGroup: 1, atomRank: 1)
// CHECK: [[RETTHEN_G2R3]] = !DILocation({{.*}}atomGroup: 2, atomRank: 3)
// CHECK: [[RETTHEN_G2R2]] = !DILocation({{.*}}atomGroup: 2, atomRank: 2)
// CHECK: [[RETTHEN_G2R1]] = !DILocation({{.*}}atomGroup: 2, atomRank: 1)
// CHECK: [[RETELSE_G3R1]] = !DILocation({{.*}}atomGroup: 3, atomRank: 1)
// CHECK: [[RET_G4R2]] = !DILocation({{.*}}atomGroup: 4, atomRank: 2)
// CHECK: [[RET_G4R1]] = !DILocation({{.*}}atomGroup: 4, atomRank: 1)

// CHECK: [[SWITCH_G2R2]] = !DILocation({{.*}}atomGroup: 2, atomRank: 2)
// CHECK: [[SWITCH_G1R1]] = !DILocation({{.*}}atomGroup: 1, atomRank: 1)
// CHECK: [[SWITCH_G2R1]] = !DILocation({{.*}}atomGroup: 2, atomRank: 1)
// CHECK: [[SWITCH_G3R1]] = !DILocation({{.*}}atomGroup: 3, atomRank: 1)
// CHECK: [[SWITCH_RET]] = !DILocation({{.*}}atomGroup: 4, atomRank: 1)

// CHECK: [[CMP_ASSIGN_LOAD0DBG]] = !DILocation(line: 57, scope: ![[#]])
// CHECK: [[CMP_ASSIGN_G1R2]] = !DILocation({{.*}}atomGroup: 1, atomRank: 2)
// CHECK: [[CMP_ASSIGN_G1R1]] = !DILocation({{.*}}atomGroup: 1, atomRank: 1)
// CHECK: [[CMP_ASSIGN_HLOADDBG]] = !DILocation(line: 58, scope: ![[#]])
// CHECK: [[CMP_ASSIGN_G3R2]] = !DILocation({{.*}}atomGroup: 3, atomRank: 2)
// CHECK: [[CMP_ASSIGN_G3R1]] = !DILocation({{.*}}atomGroup: 3, atomRank: 1)
// CHECK: [[CMP_ASSIGN_G2R2]] = !DILocation({{.*}}atomGroup: 2, atomRank: 2)
// CHECK: [[CMP_ASSIGN_G2R1]] = !DILocation({{.*}}atomGroup: 2, atomRank: 1)
// CHECK: [[CMP_ASSIGN_RET]] = !DILocation(line: 59, scope: ![[#]], atomGroup: 4, atomRank: 1)

// CHECK: [[GOTO_G1R1]] = !DILocation(line: 62, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK: [[GOTO_STORE1]] = !DILocation(line: 64, scope: ![[#]], atomGroup: 2, atomRank: 1)
// CHECK: [[GOTO_G2R1]] = !DILocation(line: 66, scope: ![[#]], atomGroup: 3, atomRank: 1)
// CHECK: [[GOTO_G3R1]] = !DILocation(line: 67, scope: ![[#]], atomGroup: 4, atomRank: 1)
// CHECK: [[GOTO_STORE2]] = !DILocation(line: 69, scope: ![[#]], atomGroup: 5, atomRank: 1)
// CHECK: [[GOTO_G4R1]] = !DILocation(line: 71, scope: ![[#]], atomGroup: 6, atomRank: 1)
// CHECK: [[GOTO_STORE3]] = !DILocation(line: 73, scope: ![[#]], atomGroup: 7, atomRank: 1)
// CHECK: [[GOTO_RET]] = !DILocation(line: 74, scope: ![[#]], atomGroup: 8, atomRank: 1)

// CHECK-DAG: [[VAARG_G1R1]] = !DILocation(line: 81, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK-DAG: [[VAARG_G1R2]] = !DILocation(line: 81, scope: ![[#]], atomGroup: 1, atomRank: 2)

// CHECK-CXX-LABEL: define dso_local void @_Z12if_cond_decli(
// The incoming-argument store is part of the prologue and carries no location,
// matching classic CodeGen (bare even under Key Instructions).
// CHECK-CXX: store i32 %0, ptr %[[A_ADDR:[0-9]+]], align 4{{$}}
// CHECK-CXX: %[[A_LOAD:[0-9]+]] = load i32, ptr %[[A_ADDR]], align 4, !dbg [[CXX_G1R2:!.*]]
// CHECK-CXX: store i32 %[[A_LOAD]], ptr %[[B_ADDR:[0-9]+]], align 4, !dbg [[CXX_G1R1:!.*]]
// CHECK-CXX: %[[B_LOAD:[0-9]+]] = load i32, ptr %[[B_ADDR]], align 4, !dbg [[CXX_B_LOADDBG:!.*]]
// CHECK-CXX: %[[B_CMP:[0-9]+]] = icmp ne i32 %[[B_LOAD]], 0, !dbg [[CXX_G2R2:!.*]]
// CHECK-CXX: br i1 %[[B_CMP]], label %{{.*}}, label %{{.*}}, !dbg [[CXX_G2R1:!.*]]
// CHECK-CXX: ret void, !dbg [[CXX_RET:!.*]]
// CHECK-CXX-LABEL: define dso_local void @_Z15try_catch_valuev(
// CHECK-CXX: %[[EXN_BEGIN:[0-9]+]] = call ptr @__cxa_begin_catch(ptr %{{.*}})
// CHECK-CXX: %[[EXN_LOAD:[0-9]+]] = load i32, ptr %[[EXN_BEGIN]], align 4, !dbg [[TRY_G1R2:!.*]]
// CHECK-CXX: store i32 %[[EXN_LOAD]], ptr %{{.*}}, align 4, !dbg [[TRY_G1R1:!.*]]
// CHECK-CXX: call void @__cxa_end_catch()
// CHECK-CXX: ret void, !dbg [[TRY_RET:!.*]]
// CHECK-CXX-DAG: [[CXX_G1R2]] = !DILocation({{.*}}atomGroup: 1, atomRank: 2)
// CHECK-CXX-DAG: [[CXX_G1R1]] = !DILocation({{.*}}atomGroup: 1, atomRank: 1)
// CHECK-CXX-DAG: [[CXX_B_LOADDBG]] = !DILocation(line: 88, scope: ![[#]])
// CHECK-CXX-DAG: [[CXX_G2R2]] = !DILocation({{.*}}atomGroup: 2, atomRank: 2)
// CHECK-CXX-DAG: [[CXX_G2R1]] = !DILocation({{.*}}atomGroup: 2, atomRank: 1)
// CHECK-CXX-DAG: [[CXX_RET]] = !DILocation({{.*}}atomGroup: 3, atomRank: 1)
// CHECK-CXX-DAG: [[TRY_G1R2]] = !DILocation(line: 95, scope: ![[#]], atomGroup: 1, atomRank: 2)
// CHECK-CXX-DAG: [[TRY_G1R1]] = !DILocation(line: 95, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK-CXX-DAG: [[TRY_RET]] = !DILocation({{.*}}atomGroup: 2, atomRank: 1)
