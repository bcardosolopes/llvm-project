// RUN: %clang_cc1 -fopenmp -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -fclangir -emit-llvm-bc %s -o %t-host.bc
// RUN: %clang_cc1 -fopenmp -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=HOST
// RUN: %clang_cc1 -fopenmp -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -fclangir -emit-llvm %s -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -o - | FileCheck %s --check-prefix=DEVICE
// RUN: %clang_cc1 -fopenmp -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG-HOST
// RUN: %clang_cc1 -fopenmp -triple powerpc64le-unknown-unknown -fopenmp-targets=powerpc64le-ibm-linux-gnu -emit-llvm %s -fopenmp-is-target-device -fopenmp-host-ir-file-path %t-host.bc -o - | FileCheck %s --check-prefix=OGCG-DEVICE

int g = 7;
#pragma omp declare target to(g)

#pragma omp declare target
int read_to(void) {
  return g;
}
#pragma omp end declare target

// HOST: @g = global i32 7, align 4
// HOST-NOT: @g_decl_tgt_ref_ptr
// HOST: @.offloading.entry.g = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 0, ptr @g
// HOST-LABEL: define dso_local i32 @read_to()
// HOST: %[[VAL:.*]] = load i32, ptr @g, align 4
// HOST: ret i32 %[[VAL]]

// DEVICE: @g = global i32 7, align 4
// DEVICE-NOT: @g_decl_tgt_ref_ptr
// DEVICE-LABEL: define hidden i32 @read_to()
// DEVICE: %[[VAL:.*]] = load i32, ptr @g, align 4
// DEVICE: ret i32 %[[VAL]]

// OGCG-HOST: @g = global i32 7, align 4
// OGCG-HOST-NOT: @g_decl_tgt_ref_ptr
// OGCG-HOST: @.offloading.entry.g = weak constant %struct.__tgt_offload_entry { i64 0, i16 1, i16 1, i32 0, ptr @g
// OGCG-HOST-LABEL: define dso_local signext i32 @read_to()
// OGCG-HOST: %[[VAL:.*]] = load i32, ptr @g, align 4
// OGCG-HOST: ret i32 %[[VAL]]

// OGCG-DEVICE: @g = global i32 7, align 4
// OGCG-DEVICE-NOT: @g_decl_tgt_ref_ptr
// OGCG-DEVICE-LABEL: define hidden signext i32 @read_to()
// OGCG-DEVICE: %[[VAL:.*]] = load i32, ptr @g, align 4
// OGCG-DEVICE: ret i32 %[[VAL]]
