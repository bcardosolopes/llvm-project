// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -disable-red-zone -coverage-data-file=/dev/null -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -emit-llvm -disable-red-zone -coverage-notes-file=aaa.gcno -coverage-data-file=bbb.gcda -debug-info-kind=limited -dwarf-version=4 -fclangir %s -o - | FileCheck %s --check-prefix=CIR-GCOV-FILE-INFO

int test1(int a) {
  switch (a % 2) {
  case 0:
    ++a;
  case 1:
    a /= 2;
  }
  return a;
}

int test2(int b) {
  return b * 2;
}

// CIR: @__llvm_gcov_ctr = internal global [3 x i64] zeroinitializer
// CIR: @__llvm_gcov_ctr.1 = internal global [1 x i64] zeroinitializer
// CIR: @__llvm_internal_gcov_emit_function_args.0 = internal unnamed_addr constant [2 x %emit_function_args_ty]
// CIR: @__llvm_internal_gcov_emit_file_info = internal unnamed_addr constant [1 x %file_info]
// CIR: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 0, ptr @__llvm_gcov_init, ptr null }]
// CIR: define internal void @__llvm_gcov_writeout() unnamed_addr
// CIR: define internal void @__llvm_gcov_reset() unnamed_addr
// CIR: define internal void @__llvm_gcov_init() unnamed_addr

// CIR-GCOV-FILE-INFO: !llvm.gcov = !{![[GCOV:[0-9]+]]}
// CIR-GCOV-FILE-INFO: ![[GCOV]] = !{!"aaa.gcno", !"bbb.gcda", !{{[0-9]+}}}
