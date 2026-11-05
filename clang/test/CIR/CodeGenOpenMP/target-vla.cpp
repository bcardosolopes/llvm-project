// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -Wno-vla -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -Wno-vla -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -Wno-vla -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -Wno-vla -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null

void target_vla_param(int n, double (*cn)[n]) {
#pragma omp target
  { cn[1][3] += 1.0; }
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_vla_paramiPA_d(
// CIR: %[[SIZE_ADDR:.*]] = cir.alloca !u64i, !cir.ptr<!u64i>, ["__omp_vla_size"]
// CIR: cir.store align(8) %[[SIZE_VALUE:.*]], %[[SIZE_ADDR]] : !u64i, !cir.ptr<!u64i>
// CIR: omp.target {{.*}}private(@{{.*}} %{{.*}} -> %{{arg[0-9]+}} [map_idx=0], @__omp_firstprivate_vla_size %[[SIZE_ADDR]] -> %[[SIZE_PRIV:arg[0-9]+]] [map_idx=1]
// CIR: %[[CAP_SIZE:.*]] = cir.load align(8) %[[SIZE_PRIV]] : !cir.ptr<!u64i>, !u64i
// CIR: %[[ONE:.*]] = cir.const #cir.int<1> : !u64i
// CIR: %[[ROW:.*]] = cir.binop(mul, %[[ONE]], %[[CAP_SIZE]]) nsw : !u64i

void target_teams_vla_nowait(int n, double (*cn)[n]) {
#pragma omp target teams nowait
  { cn[1][3] += 1.0; }
}

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_teams_vla_nowaitiPA_d(
// CIR: %[[TEAMS_SIZE_ADDR:.*]] = cir.alloca !u64i, !cir.ptr<!u64i>, ["__omp_vla_size"]
// CIR: cir.store align(8) %[[TEAMS_SIZE_VALUE:.*]], %[[TEAMS_SIZE_ADDR]] : !u64i, !cir.ptr<!u64i>
// CIR: omp.target nowait
// CIR-SAME: @__omp_firstprivate_vla_size{{(\.[0-9]+)?}} %[[TEAMS_SIZE_ADDR]] -> %[[TEAMS_SIZE_PRIV:arg[0-9]+]] [map_idx=1]
// CIR: %[[TEAMS_CAP_SIZE:.*]] = cir.load align(8) %[[TEAMS_SIZE_PRIV]] : !cir.ptr<!u64i>, !u64i
// CIR: omp.teams
// CIR: %[[TEAMS_ONE:.*]] = cir.const #cir.int<1> : !u64i
// CIR: %[[TEAMS_ROW:.*]] = cir.binop(mul, %[[TEAMS_ONE]], %[[TEAMS_CAP_SIZE]]) nsw : !u64i
