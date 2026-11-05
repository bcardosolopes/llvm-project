// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s -check-prefix=OGCG

// CIR-DAG: cir.global external @defn = #cir.undef : !s32i
// LLVM-DAG: @defn = global i32 undef
// OGCG-DAG: @defn = global i32 undef
int defn  [[clang::loader_uninitialized]];

// CIR-DAG: cir.global "private" internal dso_local @_ZL11defn_static = #cir.undef : !s32i
// LLVM-DAG: @_ZL11defn_static = internal global i32 undef
// OGCG-DAG: @_ZL11defn_static = internal global i32 undef
static int defn_static [[clang::loader_uninitialized]] __attribute__((used));

// CIR-DAG: cir.global "private" internal dso_local @_ZZ4funcvE4data = #cir.undef : !s32i
// LLVM-DAG: @_ZZ4funcvE4data = internal global i32 undef
// OGCG-DAG: @_ZZ4funcvE4data = internal global i32 undef
int* func(void)
{
  static int data [[clang::loader_uninitialized]];
  return &data;
}

class trivial
{
  float x;
};

// CIR-DAG: cir.global external @ut = #cir.undef : !rec_trivial
// LLVM-DAG: @ut = global %class.trivial undef
// OGCG-DAG: @ut = global %class.trivial undef
trivial ut [[clang::loader_uninitialized]];

// CIR-DAG: cir.global external @arr = #cir.undef : !cir.array<!cir.double x 32>
// LLVM-DAG: @arr = global [32 x double] undef
// OGCG-DAG: @arr = global [32 x double] undef
double arr[32] __attribute__((loader_uninitialized));
