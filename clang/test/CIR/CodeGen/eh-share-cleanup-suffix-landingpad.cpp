// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM

struct Guard {
  Guard();
  ~Guard();
};

void may_throw();

void test_shared_cleanup_suffix() {
  Guard a, b, c;
  may_throw();
}

// Three nested cir.cleanup.scopes give three landing pads -- one per EH scope
// depth, not one per throwing call -- and each pad falls through into the
// suffix of destructors below it: ~c chains to ~b chains to ~a, which resumes.
// LLVM-LABEL: define dso_local void @_Z{{[0-9]+}}test_shared_cleanup_suffixv(
// LLVM: call void @_ZN5GuardC1Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[A:.*]])
// LLVM: invoke void @_ZN5GuardC1Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[B:.*]])
// LLVM-NEXT: to label %{{.*}} unwind label %[[ROOT_A:.*]]
// LLVM: invoke void @_ZN5GuardC1Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[C:.*]])
// LLVM-NEXT: to label %{{.*}} unwind label %[[ROOT_B:.*]]
// LLVM: invoke void @_Z9may_throwv()
// LLVM-NEXT: to label %{{.*}} unwind label %[[ROOT_C:.*]]
// LLVM: [[ROOT_C]]:
// LLVM-NEXT: %{{.*}} = landingpad { ptr, i32 }
// LLVM-NEXT: cleanup
// LLVM-NEXT: %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 0
// LLVM-NEXT: %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 1
// LLVM-NEXT: br label %[[CLEANUP_C:.*]]
// LLVM: [[CLEANUP_C]]:
// LLVM-NEXT: %{{.*}} = phi ptr
// LLVM-NEXT: %{{.*}} = phi i32
// LLVM-NEXT: call void @_ZN5GuardD1Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[C]]) #{{[0-9]+}}
// LLVM-NEXT: br label %[[CLEANUP_B:.*]]
// LLVM: [[ROOT_B]]:
// LLVM-NEXT: %{{.*}} = landingpad { ptr, i32 }
// LLVM-NEXT: cleanup
// LLVM-NEXT: %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 0
// LLVM-NEXT: %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 1
// LLVM-NEXT: br label %[[CLEANUP_B]]
// LLVM: [[CLEANUP_B]]:
// LLVM-NEXT: %{{.*}} = phi ptr
// LLVM-NEXT: %{{.*}} = phi i32
// LLVM-NEXT: call void @_ZN5GuardD1Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[B]]) #{{[0-9]+}}
// LLVM-NEXT: br label %[[CLEANUP_A:.*]]
// LLVM: [[ROOT_A]]:
// LLVM-NEXT: %{{.*}} = landingpad { ptr, i32 }
// LLVM-NEXT: cleanup
// LLVM-NEXT: %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 0
// LLVM-NEXT: %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 1
// LLVM-NEXT: br label %[[CLEANUP_A]]
// LLVM: [[CLEANUP_A]]:
// LLVM-NEXT: %{{.*}} = phi ptr
// LLVM-NEXT: %{{.*}} = phi i32
// LLVM-NEXT: call void @_ZN5GuardD1Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[A]]) #{{[0-9]+}}
// LLVM-NEXT: %{{.*}} = insertvalue { ptr, i32 } poison, ptr %{{.*}}, 0
// LLVM-NEXT: %{{.*}} = insertvalue { ptr, i32 } %{{.*}}, i32 %{{.*}}, 1
// LLVM-NEXT: resume { ptr, i32 } %{{.*}}
