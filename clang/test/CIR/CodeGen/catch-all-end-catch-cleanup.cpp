// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat -fcxx-exceptions -fexceptions %s -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir %s --check-prefix=CIR-FLAT
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

struct Cleanup {
  ~Cleanup();
};

void may_throw();

void catch_all_runs_outer_cleanup_on_end_catch_unwind() {
  Cleanup cleanup;
  try {
    may_throw();
  } catch (...) {
  }
}

// CIR-FLAT-LABEL: cir.func{{.*}} @_Z48catch_all_runs_outer_cleanup_on_end_catch_unwindv
// CIR-FLAT: cir.catch_param end {cir.end_catch_normal_cleanup}

// LLVM-LABEL: define {{.*}} @_Z48catch_all_runs_outer_cleanup_on_end_catch_unwindv
// LLVM: call ptr @__cxa_begin_catch
// LLVM: invoke void @__cxa_end_catch()
// LLVM: to label %[[CONT:.*]] unwind label %[[LPAD:.*]]
// LLVM: [[LPAD]]:
// LLVM: landingpad { ptr, i32 }
// LLVM: cleanup
// LLVM: call void @_ZN7CleanupD1Ev(
// LLVM: resume { ptr, i32 }
// LLVM: [[CONT]]:
// LLVM: call void @_ZN7CleanupD1Ev(
// LLVM: ret void

// OGCG-LABEL: define {{.*}} @_Z48catch_all_runs_outer_cleanup_on_end_catch_unwindv
// OGCG: call ptr @__cxa_begin_catch
// OGCG: invoke void @__cxa_end_catch()
// OGCG: to label %[[CONT:.*]] unwind label %[[LPAD:.*]]
// OGCG: [[CONT]]:
// OGCG: call void @_ZN7CleanupD1Ev(
// OGCG: ret void
// OGCG: [[LPAD]]:
// OGCG: landingpad { ptr, i32 }
// OGCG: cleanup
// OGCG: call void @_ZN7CleanupD1Ev(
// OGCG: resume { ptr, i32 }

// When there's a potentially-throwing call after the try-catch block,
// __cxa_end_catch and the subsequent call should share the same cleanup
// landing pad (not clone it).
void catch_all_shared_cleanup_with_subsequent_invoke() {
  Cleanup cleanup;
  try {
    may_throw();
  } catch (...) {
  }
  may_throw();
}

// CIR-FLAT-LABEL: cir.func{{.*}} @_Z47catch_all_shared_cleanup_with_subsequent_invokev
// CIR-FLAT: cir.catch_param end {cir.end_catch_normal_cleanup}

// LLVM-LABEL: define {{.*}} @_Z47catch_all_shared_cleanup_with_subsequent_invokev
// LLVM: call ptr @__cxa_begin_catch
// LLVM: invoke void @__cxa_end_catch()
// LLVM-NEXT: to label %{{.*}} unwind label %[[SHARED_LPAD:.*]]
// LLVM: invoke void @_Z9may_throwv()
// LLVM-NEXT: to label %{{.*}} unwind label %[[SHARED_LPAD]]
// LLVM: [[SHARED_LPAD]]:
// LLVM: landingpad { ptr, i32 }
// LLVM: cleanup
// LLVM: call void @_ZN7CleanupD1Ev(
// LLVM: resume { ptr, i32 }

// OGCG-LABEL: define {{.*}} @_Z47catch_all_shared_cleanup_with_subsequent_invokev
// OGCG: call ptr @__cxa_begin_catch
// OGCG: invoke void @__cxa_end_catch()
// OGCG-NEXT: to label %{{.*}} unwind label %[[SHARED_LPAD:.*]]
// OGCG: invoke void @_Z9may_throwv()
// OGCG-NEXT: to label %{{.*}} unwind label %[[SHARED_LPAD]]
// OGCG: [[SHARED_LPAD]]:
// OGCG: landingpad { ptr, i32 }
// OGCG: cleanup
// OGCG: call void @_ZN7CleanupD1Ev(
// OGCG: resume { ptr, i32 }
