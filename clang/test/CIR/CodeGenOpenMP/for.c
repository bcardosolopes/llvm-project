// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s

void body(void);
int n;

void for_assign_init(void) {
  int i;
#pragma omp for
  for (i = 0; i < n; ++i)
    body();
}

// CHECK-LABEL: cir.func{{.*}} @for_assign_init
// CHECK: %[[OUTER_I:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CHECK: %[[LOOP_I:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CHECK: omp.wsloop {
// CHECK:   omp.loop_nest (%[[IV:.*]]) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {
// CHECK:     %{{.*}} = builtin.unrealized_conversion_cast %[[IV]] : i32 to !s32i
// CHECK:     cir.store align(4) %{{.*}}, %[[LOOP_I]] : !s32i, !cir.ptr<!s32i>
// CHECK:     cir.call @body() : () -> ()
// CHECK:     omp.yield
// CHECK:   }
// CHECK: }

void parallel_for_assign_init(void) {
  int i;
#pragma omp parallel for
  for (i = 0; i < n; ++i)
    body();
}

// CHECK-LABEL: cir.func{{.*}} @parallel_for_assign_init
// CHECK: %[[OUTER_PI:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CHECK: %[[PRIVATE_PI:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CHECK: omp.parallel private(@{{.*}} %[[PRIVATE_PI]] -> %arg{{.*}} : !cir.ptr<!s32i>) {
// CHECK:   cir.scope {
// CHECK:     omp.wsloop nowait {
// CHECK:       omp.loop_nest (%[[PIV:.*]]) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {
// CHECK:         %{{.*}} = builtin.unrealized_conversion_cast %[[PIV]] : i32 to !s32i
// CHECK:         cir.store align(4) %{{.*}}, %arg{{.*}} : !s32i, !cir.ptr<!s32i>
// CHECK:         cir.call @body() : () -> ()
// CHECK:         omp.yield
// CHECK:       }
// CHECK:     }
// CHECK:   }
// CHECK:   omp.terminator
// CHECK: }

void for_schedule_static(void) {
  int i;
#pragma omp for schedule(static)
  for (i = 0; i < n; ++i)
    body();
}

// CHECK-LABEL: cir.func{{.*}} @for_schedule_static
// CHECK: %[[OUTER_SI:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CHECK: %[[LOOP_SI:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i"]
// CHECK: omp.wsloop schedule(static) {
// CHECK:   omp.loop_nest (%[[SIV:.*]]) : i32 = (%{{.*}}) to (%{{.*}}) step (%{{.*}}) {
// CHECK:     %{{.*}} = builtin.unrealized_conversion_cast %[[SIV]] : i32 to !s32i
// CHECK:     cir.store align(4) %{{.*}}, %[[LOOP_SI]] : !s32i, !cir.ptr<!s32i>
// CHECK:     cir.call @body() : () -> ()
// CHECK:     omp.yield
// CHECK:   }
// CHECK: }
