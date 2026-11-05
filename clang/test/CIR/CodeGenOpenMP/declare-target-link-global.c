// RUN: %clang_cc1 -fopenmp -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -fclangir -emit-llvm-bc %s -o %t-host.bc
// RUN: %clang_cc1 -fopenmp -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=HOST
// RUN: %clang_cc1 -fopenmp -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -fclangir -emit-llvm %s -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -o - | FileCheck %s --check-prefix=DEVICE
// RUN: %clang_cc1 -fopenmp -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG-HOST
// RUN: %clang_cc1 -fopenmp -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -emit-llvm %s -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -o - | FileCheck %s --check-prefix=OGCG-DEVICE

extern int g;
#pragma omp declare target link(g)

#pragma omp declare target
int read_g(void) {
  return g;
}
#pragma omp end declare target

// HOST: @g = external global i32, align 4
// HOST: @g_decl_tgt_ref_ptr = weak global ptr @g, align 8
// HOST: @.offloading.entry.g_decl_tgt_ref_ptr = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 1, ptr @g_decl_tgt_ref_ptr
// HOST-LABEL: define dso_local i32 @read_g()
// HOST: %[[VAL:.*]] = load i32, ptr @g, align 4
// HOST: ret i32 %[[VAL]]

// DEVICE-NOT: @g =
// DEVICE: @g_decl_tgt_ref_ptr = weak global ptr null, align 8
// DEVICE-LABEL: define hidden i32 @read_g()
// DEVICE: %[[REF:.*]] = load ptr, ptr @g_decl_tgt_ref_ptr, align 8
// DEVICE: %[[VAL:.*]] = load i32, ptr %[[REF]], align 4
// DEVICE: ret i32 %[[VAL]]

// OGCG-HOST-DAG: @g = external global i32, align 4
// OGCG-HOST-DAG: @g_decl_tgt_ref_ptr = weak global ptr @g, align 8
// OGCG-HOST-DAG: @.offloading.entry.g_decl_tgt_ref_ptr = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 1, ptr @g_decl_tgt_ref_ptr
// OGCG-HOST-LABEL: define dso_local signext i32 @read_g()
// OGCG-HOST: %[[VAL:.*]] = load i32, ptr @g, align 4
// OGCG-HOST: ret i32 %[[VAL]]

// OGCG-DEVICE-NOT: @g =
// OGCG-DEVICE: @g_decl_tgt_ref_ptr = weak global ptr null, align 8
// OGCG-DEVICE-LABEL: define hidden signext i32 @read_g()
// OGCG-DEVICE: %[[REF:.*]] = load ptr, ptr @g_decl_tgt_ref_ptr, align {{[0-9]+}}
// OGCG-DEVICE: %[[VAL:.*]] = load i32, ptr %[[REF]], align 4
// OGCG-DEVICE: ret i32 %[[VAL]]
