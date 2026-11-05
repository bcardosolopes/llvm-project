// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s

llvm.func @foo()

// CHECK-LABEL: @omp_teams_simple
// CHECK: call void {{.*}} @__kmpc_fork_teams(ptr @{{.+}}, i32 0, ptr [[OUTLINED_FN:.+]])
// CHECK: ret void
llvm.func @omp_teams_simple() {
    omp.teams {
        llvm.call @foo() : () -> ()
        omp.terminator
    }
    llvm.return
}

// CHECK: define internal void @[[OUTLINED_FN:.+]](ptr {{.+}}, ptr {{.+}})
// CHECK:   call void @foo()
// CHECK:   ret void

// -----

llvm.func @foo()

// CHECK-LABEL: @omp_teams_func_attrs
// CHECK: call void {{.*}} @__kmpc_fork_teams(ptr @{{.+}}, i32 0, ptr @[[OUTLINED_FN:.+]])
llvm.func @omp_teams_func_attrs() attributes {
    target_cpu = "x86-64",
    target_features = #llvm.target_features<["+mmx", "+sse"]>
} {
    omp.teams {
        llvm.call @foo() : () -> ()
        omp.terminator
    }
    llvm.return
}

// CHECK:      define internal void @[[OUTLINED_FN]](ptr {{.+}}, ptr {{.+}})
// CHECK-SAME: #[[ATTR:[0-9]+]]
// CHECK:      call void @foo()

// CHECK: attributes #[[ATTR]] = {
// CHECK-SAME: "target-cpu"="x86-64"
// CHECK-SAME: "target-features"="+mmx,+sse"

// -----

llvm.func @foo(i32) -> ()

// CHECK-LABEL: @omp_teams_shared_simple
// CHECK-SAME: (i32 [[ARG0:%.+]])
// CHECK: br
// CHECK: call void {{.+}} @__kmpc_fork_teams(ptr @{{.+}}, i32 1, ptr [[OUTLINED_FN:.+]], i32 [[ARG0]])
// CHECK: ret void
llvm.func @omp_teams_shared_simple(%arg0: i32) {
    omp.teams {
        llvm.call @foo(%arg0) : (i32) -> ()
        omp.terminator
    }
    llvm.return
}

// CHECK: define internal void [[OUTLINED_FN:@.+]](ptr {{.+}}, ptr {{.+}}, i32 [[ARG0:%.+]])
// CHECK:   call void @foo(i32 [[ARG0]])
// CHECK:   ret void

// -----

llvm.func @my_alloca_fn() -> !llvm.ptr
llvm.func @foo(i32, f32, !llvm.ptr, f128, !llvm.ptr, i32) -> ()
llvm.func @bar()

// CHECK-LABEL: @omp_teams_branching_shared
// CHECK-SAME: (i1 [[CONDITION:%.+]], i32 [[ARG0:%.+]], float [[ARG1:%.+]], ptr [[ARG2:%.+]], fp128 [[ARG3:%.+]])

// Checking that the helper-specific captures are materialized before outlining.
// CHECK: [[ALLOCATED:%.+]] = call ptr @my_alloca_fn()
// CHECK: [[LOADED:%.+]] = load i32, ptr [[ALLOCATED]]
// CHECK: br label

// Runtime call.
// CHECK: call void {{.+}} @__kmpc_fork_teams(ptr @{{.+}}, i32 7, ptr [[OUTLINED_FN:[^,]+]], i1 [[CONDITION]], i32 [[ARG0]], float [[ARG1]], ptr [[ARG2]], fp128 [[ARG3]], ptr [[ALLOCATED]], i32 [[LOADED]])
// CHECK: br label
// CHECK: call void @bar()
// CHECK: ret void
llvm.func @omp_teams_branching_shared(%condition: i1, %arg0: i32, %arg1: f32, %arg2: !llvm.ptr, %arg3: f128) {
    %allocated = llvm.call @my_alloca_fn(): () -> !llvm.ptr
    %loaded = llvm.load %allocated : !llvm.ptr -> i32
    llvm.br ^codegenBlock
^codegenBlock:
    omp.teams {
        llvm.cond_br %condition, ^true_block, ^false_block
    ^true_block:
        llvm.call @foo(%arg0, %arg1, %arg2, %arg3, %allocated, %loaded) : (i32, f32, !llvm.ptr, f128, !llvm.ptr, i32) -> ()
        llvm.br ^exit
    ^false_block:
        llvm.br ^exit
    ^exit:
        omp.terminator
    }
    llvm.call @bar() : () -> ()
    llvm.return
}

// Check the outlined function.
// CHECK: define internal void [[OUTLINED_FN]](ptr {{.+}}, ptr {{.+}}, i1 [[CONDITION_INNER:%.+]], i32 [[ARG0_INNER:%.+]], float [[ARG1_INNER:%.+]], ptr [[ARG2_INNER:%.+]], fp128 [[ARG3_INNER:%.+]], ptr [[ALLOCATED_INNER:%.+]], i32 [[LOADED_INNER:%.+]])
// CHECK:   br label

// CHECK:   br i1 [[CONDITION_INNER]], label %[[TRUE:.+]], label %[[FALSE:.+]]
// CHECK: [[FALSE]]:
// CHECK-NEXT: br label
// CHECK: [[TRUE]]:
// CHECK:   call void @foo(i32 [[ARG0_INNER]], float [[ARG1_INNER]], ptr [[ARG2_INNER]], fp128 [[ARG3_INNER]], ptr [[ALLOCATED_INNER]], i32 [[LOADED_INNER]])
// CHECK-NEXT: br label
// CHECK: ret void

// -----

omp.private {type = private} @teams_private_privatizer : i32 init {
^bb0(%mold: !llvm.ptr, %private: !llvm.ptr):
  %c0 = llvm.mlir.constant(0 : i32) : i32
  llvm.store %c0, %private : i32, !llvm.ptr
  omp.yield(%private: !llvm.ptr)
}

// CHECK-LABEL: @omp_teams_private
// CHECK-SAME: (ptr [[MOLD:%.+]])
// CHECK: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 0, ptr @[[OUTLINED_FN:.+]])
// CHECK: ret void
llvm.func @omp_teams_private(%x : !llvm.ptr) {
  omp.teams private(@teams_private_privatizer %x -> %arg0 : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

// CHECK: define internal void @[[OUTLINED_FN]](ptr {{.+}}, ptr {{.+}})
// CHECK: teams.alloca:
// CHECK-NEXT: %[[PRIVATE_ALLOC:.*]] = alloca i32, align 4
// CHECK: omp.private.init:
// CHECK-NEXT: store i32 0, ptr %[[PRIVATE_ALLOC]], align 4
// CHECK: ret void

// -----

llvm.func @foo_free(!llvm.ptr)

omp.private {type = firstprivate} @teams_firstprivate_privatizer : f32 copy {
^bb0(%shared: !llvm.ptr, %private: !llvm.ptr):
  %0 = llvm.load %shared : !llvm.ptr -> f32
  llvm.store %0, %private : f32, !llvm.ptr
  omp.yield(%private : !llvm.ptr)
} dealloc {
^bb0(%private: !llvm.ptr):
  llvm.call @foo_free(%private) : (!llvm.ptr) -> ()
  omp.yield
}

// CHECK-LABEL: @omp_teams_firstprivate
// CHECK-SAME: (ptr [[SHARED:%.+]])
// CHECK: [[SHARED_VAL:%.+]] = load float, ptr [[SHARED]], align 4
// CHECK: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 1, ptr @[[OUTLINED_FN:.+]], float [[SHARED_VAL]])
// CHECK: ret void
llvm.func @omp_teams_firstprivate(%x : !llvm.ptr) {
  omp.teams private(@teams_firstprivate_privatizer %x -> %arg0 : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

// CHECK: define internal void @[[OUTLINED_FN]](ptr {{.+}}, ptr {{.+}}, float [[SHARED_VAL:%.+]])
// CHECK: teams.alloca:
// CHECK-NEXT: %[[PRIVATE_ALLOC:.*]] = alloca float, align 4
// CHECK: omp.private.copy:
// CHECK-NEXT: store float [[SHARED_VAL]], ptr %[[PRIVATE_ALLOC]], align 4
// CHECK: omp.region.cont:
// CHECK-NEXT: call void @foo_free(ptr %[[PRIVATE_ALLOC]])
// CHECK: ret void

// -----

llvm.func @beforeTeams()
llvm.func @duringTeams()
llvm.func @afterTeams()

// CHECK-LABEL: @omp_teams_thread_limit
// CHECK-SAME: (i32 [[THREAD_LIMIT:.+]])
llvm.func @omp_teams_thread_limit(%threadLimit: i32) {
    // CHECK-NEXT: call void @beforeTeams()
    llvm.call @beforeTeams() : () -> ()
    // CHECK: [[THREAD_NUM:%.+]] = call i32 @__kmpc_global_thread_num
    // CHECK-NEXT: call void @__kmpc_push_num_teams_51({{.+}}, i32 [[THREAD_NUM]], i32 0, i32 0, i32 [[THREAD_LIMIT]])
    // CHECK: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 0, ptr [[OUTLINED_FN:.+]])
    omp.teams thread_limit(%threadLimit : i32) {
        llvm.call @duringTeams() : () -> ()
        omp.terminator
    }
    // CHECK: call void @afterTeams
    llvm.call @afterTeams() : () -> ()
    // CHECK: ret void
    llvm.return
}

// CHECK: define internal void [[OUTLINED_FN]](ptr {{.+}}, ptr {{.+}})
// CHECK: call void @duringTeams()
// CHECK: ret void

// -----

llvm.func @beforeTeams()
llvm.func @duringTeams()
llvm.func @afterTeams()

// CHECK-LABEL: @omp_teams_num_teams_upper
// CHECK-SAME: (i32 [[NUM_TEAMS_UPPER:.+]])
llvm.func @omp_teams_num_teams_upper(%numTeamsUpper: i32) {
    // CHECK-NEXT: call void @beforeTeams()
    llvm.call @beforeTeams() : () -> ()
    // CHECK: [[THREAD_NUM:%.+]] = call i32 @__kmpc_global_thread_num
    // CHECK-NEXT: call void @__kmpc_push_num_teams_51({{.+}}, i32 [[THREAD_NUM]], i32 [[NUM_TEAMS_UPPER]], i32 [[NUM_TEAMS_UPPER]], i32 0)
    // CHECK: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 0, ptr [[OUTLINED_FN:.+]])
    omp.teams num_teams(to %numTeamsUpper : i32) {
        llvm.call @duringTeams() : () -> ()
        omp.terminator
    }
    // CHECK: call void @afterTeams
    llvm.call @afterTeams() : () -> ()
    // CHECK: ret void
    llvm.return
}

// CHECK: define internal void [[OUTLINED_FN]](ptr {{.+}}, ptr {{.+}})
// CHECK: call void @duringTeams()
// CHECK: ret void

// -----

llvm.func @beforeTeams()
llvm.func @duringTeams()
llvm.func @afterTeams()

// CHECK-LABEL: @omp_teams_num_teams_lower_and_upper
// CHECK-SAME: (i32 [[NUM_TEAMS_LOWER:.+]], i32 [[NUM_TEAMS_UPPER:.+]])
llvm.func @omp_teams_num_teams_lower_and_upper(%numTeamsLower: i32, %numTeamsUpper: i32) {
    // CHECK-NEXT: call void @beforeTeams()
    llvm.call @beforeTeams() : () -> ()
    // CHECK: [[THREAD_NUM:%.+]] = call i32 @__kmpc_global_thread_num
    // CHECK-NEXT: call void @__kmpc_push_num_teams_51({{.+}}, i32 [[THREAD_NUM]], i32 [[NUM_TEAMS_LOWER]], i32 [[NUM_TEAMS_UPPER]], i32 0)
    // CHECK: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 0, ptr [[OUTLINED_FN:.+]])
    omp.teams num_teams(%numTeamsLower : i32 to %numTeamsUpper: i32) {
        llvm.call @duringTeams() : () -> ()
        omp.terminator
    }
    // CHECK: call void @afterTeams
    llvm.call @afterTeams() : () -> ()
    // CHECK: ret void
    llvm.return
}

// CHECK: define internal void [[OUTLINED_FN]](ptr {{.+}}, ptr {{.+}})
// CHECK: call void @duringTeams()
// CHECK: ret void

// -----

llvm.func @beforeTeams()
llvm.func @duringTeams()
llvm.func @afterTeams()

// CHECK-LABEL: @omp_teams_num_teams_and_thread_limit
// CHECK-SAME: (i32 [[NUM_TEAMS_LOWER:.+]], i32 [[NUM_TEAMS_UPPER:.+]], i32 [[THREAD_LIMIT:.+]])
llvm.func @omp_teams_num_teams_and_thread_limit(%numTeamsLower: i32, %numTeamsUpper: i32, %threadLimit: i32) {
    // CHECK-NEXT: call void @beforeTeams()
    llvm.call @beforeTeams() : () -> ()
    // CHECK: [[THREAD_NUM:%.+]] = call i32 @__kmpc_global_thread_num
    // CHECK-NEXT: call void @__kmpc_push_num_teams_51({{.+}}, i32 [[THREAD_NUM]], i32 [[NUM_TEAMS_LOWER]], i32 [[NUM_TEAMS_UPPER]], i32 [[THREAD_LIMIT]])
    // CHECK: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 0, ptr [[OUTLINED_FN:.+]])
    omp.teams num_teams(%numTeamsLower : i32 to %numTeamsUpper: i32) thread_limit(%threadLimit: i32) {
        llvm.call @duringTeams() : () -> ()
        omp.terminator
    }
    // CHECK: call void @afterTeams
    llvm.call @afterTeams() : () -> ()
    // CHECK: ret void
    llvm.return
}

// CHECK: define internal void [[OUTLINED_FN]](ptr {{.+}}, ptr {{.+}})
// CHECK: call void @duringTeams()
// CHECK: ret void

// -----

llvm.func @beforeTeams()
llvm.func @duringTeams()
llvm.func @afterTeams()

// CHECK-LABEL: @teams_if
// CHECK-SAME: (i1 [[ARG:.+]])
llvm.func @teams_if(%arg : i1) {
    // CHECK-NEXT: call void @beforeTeams()
    llvm.call @beforeTeams() : () -> ()
    // If the condition is true, then the value of bounds is zero - which basically means "implementation-defined". 
    // The runtime sees zero and sets a default value of number of teams. This behavior is according to the standard.
    // The same is true for `thread_limit`.
    // CHECK: [[NUM_TEAMS_UPPER:%.+]] = select i1 [[ARG]], i32 0, i32 1
    // CHECK: [[NUM_TEAMS_LOWER:%.+]] = select i1 [[ARG]], i32 0, i32 1
    // CHECK: call void @__kmpc_push_num_teams_51(ptr {{.+}}, i32 {{.+}}, i32 [[NUM_TEAMS_LOWER]], i32 [[NUM_TEAMS_UPPER]], i32 0)
    // CHECK: call void {{.+}} @__kmpc_fork_teams({{.+}})
    omp.teams if(%arg) {
        llvm.call @duringTeams() : () -> ()
        omp.terminator
    }
    // CHECK: call void @afterTeams()
    llvm.call @afterTeams() : () -> ()
    llvm.return
}

// -----

llvm.func @beforeTeams()
llvm.func @duringTeams()
llvm.func @afterTeams()

// CHECK-LABEL: @teams_if_with_num_teams
// CHECK-SAME: (i1 [[CONDITION:.+]], i32 [[NUM_TEAMS_LOWER:.+]], i32 [[NUM_TEAMS_UPPER:.+]], i32 [[THREAD_LIMIT:.+]])
llvm.func @teams_if_with_num_teams(%condition: i1, %numTeamsLower: i32, %numTeamsUpper: i32, %threadLimit: i32) {
    // CHECK: call void @beforeTeams()
    llvm.call @beforeTeams() : () -> ()
    // CHECK: [[NUM_TEAMS_UPPER_NEW:%.+]] = select i1 [[CONDITION]], i32 [[NUM_TEAMS_UPPER]], i32 1
    // CHECK: [[NUM_TEAMS_LOWER_NEW:%.+]] = select i1 [[CONDITION]], i32 [[NUM_TEAMS_LOWER]], i32 1
    // CHECK: call void @__kmpc_push_num_teams_51(ptr {{.+}}, i32 {{.+}}, i32 [[NUM_TEAMS_LOWER_NEW]], i32 [[NUM_TEAMS_UPPER_NEW]], i32 [[THREAD_LIMIT]])
    // CHECK: call void {{.+}} @__kmpc_fork_teams({{.+}})
    omp.teams if(%condition) num_teams(%numTeamsLower: i32 to %numTeamsUpper: i32) thread_limit(%threadLimit: i32) {
        llvm.call @duringTeams() : () -> ()
        omp.terminator
    }
    // CHECK: call void @afterTeams()
    llvm.call @afterTeams() : () -> ()
    llvm.return
}
