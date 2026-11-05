// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple x86_64-unknown-linux-gnu -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm-bc %s -o %t-host.bc
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple x86_64-unknown-linux-gnu -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=HOST
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple x86_64-unknown-linux-gnu -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -o - | FileCheck %s --check-prefix=DEVICE
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple x86_64-unknown-linux-gnu -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm-bc %s -o %t-host-ogcg.bc
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple x86_64-unknown-linux-gnu -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=HOST
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple x86_64-unknown-linux-gnu -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host-ogcg.bc -o - | FileCheck %s --check-prefix=DEVICE

// expected-no-diagnostics

void foo() {}
#pragma omp declare target to(foo) indirect

static void bar() {}
#pragma omp declare target to(bar) indirect

[[gnu::visibility("hidden")]] void baz() { bar(); }
#pragma omp declare target to(baz) indirect

void disabled() {}
#pragma omp declare target to(disabled) indirect(false)

char var = 0;
#pragma omp declare target to(var) indirect

// HOST-DAG: @[[FOO_ENTRY_NAME:.+]] = internal unnamed_addr constant [{{[0-9]+}} x i8] c"[[FOO_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_foo_l[0-9]+]]\00"
// HOST-DAG: @.offloading.entry.[[FOO_NAME]] = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 8, ptr @_Z3foov, ptr @[[FOO_ENTRY_NAME]], i64 8, i64 0, ptr null }
// HOST-DAG: @[[BAR_ENTRY_NAME:.+]] = internal unnamed_addr constant [{{[0-9]+}} x i8] c"[[BAR_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_bar_l[0-9]+]]\00"
// HOST-DAG: @.offloading.entry.[[BAR_NAME]] = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 8, ptr @_ZL3barv, ptr @[[BAR_ENTRY_NAME]], i64 8, i64 0, ptr null }
// HOST-DAG: @[[BAZ_ENTRY_NAME:.+]] = internal unnamed_addr constant [{{[0-9]+}} x i8] c"[[BAZ_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_baz_l[0-9]+]]\00"
// HOST-DAG: @.offloading.entry.[[BAZ_NAME]] = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 8, ptr @_Z3bazv, ptr @[[BAZ_ENTRY_NAME]], i64 8, i64 0, ptr null }
// HOST-DAG: @[[VAR:.+]] = global i8 0, align 1
// HOST-DAG: @[[VAR_ENTRY_NAME:.+]] = internal unnamed_addr constant [4 x i8] c"var\00"
// HOST-DAG: @.offloading.entry.var = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 0, ptr @[[VAR]], ptr @[[VAR_ENTRY_NAME]], i64 1, i64 0, ptr null }
// HOST-DAG: !{{[0-9]+}} = !{i32 1, !"[[FOO_NAME]]", i32 8, i32 {{[0-9]+}}}
// HOST-DAG: !{{[0-9]+}} = !{i32 1, !"[[BAR_NAME]]", i32 8, i32 {{[0-9]+}}}
// HOST-DAG: !{{[0-9]+}} = !{i32 1, !"[[BAZ_NAME]]", i32 8, i32 {{[0-9]+}}}
// HOST-DAG: !{{[0-9]+}} = !{i32 1, !"var", i32 0, i32 {{[0-9]+}}}

// DEVICE-DAG: @[[FOO_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_foo_l[0-9]+]] = protected {{.*}}constant ptr @_Z3foov
// DEVICE-DAG: @[[BAR_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_bar_l[0-9]+]] = protected {{.*}}constant ptr @_ZL3barv
// DEVICE-DAG: @[[BAZ_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_baz_l[0-9]+]] = protected {{.*}}constant ptr @_Z3bazv
// DEVICE-DAG: @var = {{.*}}global i8 0, align 1
