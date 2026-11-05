// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=amdgcn-amd-amdhsa -emit-llvm %s -o - | FileCheck %s --check-prefix=HOST
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=amdgcn-amd-amdhsa -emit-llvm-bc %s -o %t-host.bc
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple amdgcn-amd-amdhsa -emit-llvm %s -fopenmp-is-target-device -fvisibility=protected -fopenmp-host-ir-file-path %t-host.bc -o - | FileCheck %s --check-prefix=DEVICE
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple amdgcn-amd-amdhsa %s -fopenmp-is-target-device -fvisibility=protected -fopenmp-host-ir-file-path %t-host.bc -emit-pch -o %t
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple amdgcn-amd-amdhsa -emit-llvm %s -fopenmp-is-target-device -fvisibility=protected -fopenmp-host-ir-file-path %t-host.bc -include-pch %t -o - | FileCheck %s --check-prefix=DEVICE
// RUN: %if cir-support %{ %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=amdgcn-amd-amdhsa -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIRHOST %}
// RUN: %if cir-support %{ %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=amdgcn-amd-amdhsa -fclangir -emit-llvm-bc %s -o %t-host-cir.bc %}
// RUN: %if cir-support %{ %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple amdgcn-amd-amdhsa -fclangir -emit-llvm %s -fopenmp-is-target-device -fvisibility=protected -fopenmp-host-ir-file-path %t-host-cir.bc -o - | FileCheck %s --check-prefix=CIRDEVICE %}

// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=spirv64-intel -emit-llvm %s -o - | FileCheck %s --check-prefix=HOST
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=spirv64-intel -emit-llvm-bc %s -o %t-spirv-host.bc
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple spirv64-intel -emit-llvm %s -fopenmp-is-target-device -fvisibility=protected -fopenmp-host-ir-file-path %t-spirv-host.bc -o - | FileCheck %s --check-prefix=DEVICE
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple spirv64-intel %s -fopenmp-is-target-device -fvisibility=protected -fopenmp-host-ir-file-path %t-spirv-host.bc -emit-pch -o %t
// RUN: %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple spirv64-intel -emit-llvm %s -fopenmp-is-target-device -fvisibility=protected -fopenmp-host-ir-file-path %t-spirv-host.bc -include-pch %t -o - | FileCheck %s --check-prefix=DEVICE
// RUN: %if cir-support %{ %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=spirv64-intel -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIRHOST %}
// RUN: %if cir-support %{ %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=spirv64-intel -fclangir -emit-llvm-bc %s -o %t-spirv-host-cir.bc %}
// RUN: %if cir-support %{ %clang_cc1 -verify -fopenmp -fopenmp-version=51 -x c++ -triple spirv64-intel -fclangir -emit-llvm %s -fopenmp-is-target-device -fvisibility=protected -fopenmp-host-ir-file-path %t-spirv-host-cir.bc -o - | FileCheck %s --check-prefix=CIRDEVICE %}

// expected-no-diagnostics
#ifndef HEADER
#define HEADER

//.
// HOST: @[[VAR:.+]] = global i8 0, align 1
// HOST: @[[FOO_ENTRY_NAME:.+]] = internal unnamed_addr constant [{{[0-9]+}} x i8] c"[[FOO_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_foo_l[0-9]+]]\00"
// HOST: @.offloading.entry.[[FOO_NAME]] = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 8, ptr @_Z3foov, ptr @[[FOO_ENTRY_NAME]], i64 8, i64 0, ptr null }
// HOST: @[[BAZ_ENTRY_NAME:.+]] = internal unnamed_addr constant [{{[0-9]+}} x i8] c"[[BAZ_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_baz_l[0-9]+]]\00"
// HOST: @.offloading.entry.[[BAZ_NAME]] = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 8, ptr @_Z3bazv, ptr @[[BAZ_ENTRY_NAME]], i64 8, i64 0, ptr null }
// HOST: @[[VAR_ENTRY_NAME:.+]] = internal unnamed_addr constant [4 x i8] c"var\00"
// HOST: @.offloading.entry.var = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 0, ptr @[[VAR]], ptr @[[VAR_ENTRY_NAME]], i64 1, i64 0, ptr null }
// HOST: @[[BAR_ENTRY_NAME:.+]] = internal unnamed_addr constant [{{[0-9]+}} x i8] c"[[BAR_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_bar_l[0-9]+]]\00"
// HOST: @.offloading.entry.[[BAR_NAME]] = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 8, ptr @_ZL3barv, ptr @[[BAR_ENTRY_NAME]], i64 8, i64 0, ptr null }
//.
// CIRHOST-DAG: @[[CIR_VAR:.+]] = global i8 0, align 1
// CIRHOST-DAG: @[[CIR_VAR_ENTRY_NAME:.+]] = internal unnamed_addr constant [4 x i8] c"var\00"
// CIRHOST-DAG: @.offloading.entry.var = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 0, ptr @[[CIR_VAR]], ptr @[[CIR_VAR_ENTRY_NAME]], i64 1, i64 0, ptr null }
// CIRHOST-DAG: @[[CIR_FOO_ENTRY_NAME:.+]] = internal unnamed_addr constant [{{[0-9]+}} x i8] c"[[CIR_FOO_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_foo_l[0-9]+]]\00"
// CIRHOST-DAG: @.offloading.entry.[[CIR_FOO_NAME]] = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 8, ptr @_Z3foov, ptr @[[CIR_FOO_ENTRY_NAME]], i64 8, i64 0, ptr null }
// CIRHOST-DAG: @[[CIR_BAR_ENTRY_NAME:.+]] = internal unnamed_addr constant [{{[0-9]+}} x i8] c"[[CIR_BAR_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_bar_l[0-9]+]]\00"
// CIRHOST-DAG: @.offloading.entry.[[CIR_BAR_NAME]] = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 8, ptr @_ZL3barv, ptr @[[CIR_BAR_ENTRY_NAME]], i64 8, i64 0, ptr null }
// CIRHOST-DAG: @[[CIR_BAZ_ENTRY_NAME:.+]] = internal unnamed_addr constant [{{[0-9]+}} x i8] c"[[CIR_BAZ_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_baz_l[0-9]+]]\00"
// CIRHOST-DAG: @.offloading.entry.[[CIR_BAZ_NAME]] = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 8, ptr @_Z3bazv, ptr @[[CIR_BAZ_ENTRY_NAME]], i64 8, i64 0, ptr null }
//.
// DEVICE: @[[FOO_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_foo_l[0-9]+]] = protected addrspace(1) constant {{ptr|ptr addrspace\(9\)}} @_Z3foov
// DEVICE: @[[BAZ_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_baz_l[0-9]+]] = protected addrspace(1) constant {{ptr|ptr addrspace\(9\)}} @_Z3bazv
// DEVICE: @var = protected addrspace(1) global i8 0, align 1
// DEVICE: @[[BAR_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_bar_l[0-9]+]] = protected addrspace(1) constant {{ptr|ptr addrspace\(9\)}} @_ZL3barv
//.
// CIRDEVICE-DAG: @var = protected addrspace(1) global i8 0, align 1
// CIRDEVICE-DAG: @[[CIRDEVICE_FOO_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_foo_l[0-9]+]] = protected addrspace(1) constant {{ptr|ptr addrspace\(9\)}} @_Z3foov
// CIRDEVICE-DAG: @[[CIRDEVICE_BAR_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_bar_l[0-9]+]] = protected addrspace(1) constant {{ptr|ptr addrspace\(9\)}} @_ZL3barv
// CIRDEVICE-DAG: @[[CIRDEVICE_BAZ_NAME:__omp_offloading_[0-9a-z]+_[0-9a-z]+_baz_l[0-9]+]] = protected addrspace(1) constant {{ptr|ptr addrspace\(9\)}} @_Z3bazv
//.
void foo() { }
#pragma omp declare target to(foo) indirect

static void bar() { }
#pragma omp declare target to(bar) indirect

[[gnu::visibility("hidden")]] void baz() { bar(); }
#pragma omp declare target to(baz) indirect

static void unused() { };
#pragma omp declare target to(unused) indirect

void disabled() { };
#pragma omp declare target to(disabled) indirect(false)

char var = 0;
#pragma omp declare target to(var) indirect

#endif
//.
// HOST-DAG: !{{[0-9]+}} = !{i32 1, !"[[FOO_NAME]]", i32 8, i32 0}
// HOST-DAG: !{{[0-9]+}} = !{i32 1, !"[[BAZ_NAME]]", i32 8, i32 1}
// HOST-DAG: !{{[0-9]+}} = !{i32 1, !"var", i32 0, i32 2}
// HOST-DAG: !{{[0-9]+}} = !{i32 1, !"[[BAR_NAME]]", i32 8, i32 3}
// CIRHOST-DAG: !{{[0-9]+}} = !{i32 1, !"[[CIR_FOO_NAME]]", i32 8, i32 {{[0-9]+}}}
// CIRHOST-DAG: !{{[0-9]+}} = !{i32 1, !"[[CIR_BAZ_NAME]]", i32 8, i32 {{[0-9]+}}}
// CIRHOST-DAG: !{{[0-9]+}} = !{i32 1, !"var", i32 0, i32 {{[0-9]+}}}
// CIRHOST-DAG: !{{[0-9]+}} = !{i32 1, !"[[CIR_BAR_NAME]]", i32 8, i32 {{[0-9]+}}}
//.
