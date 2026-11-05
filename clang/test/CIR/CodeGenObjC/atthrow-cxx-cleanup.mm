// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -fobjc-runtime=ios-16.0.0 -x objective-c++ -std=gnu++20 -fexceptions -fcxx-exceptions -fobjc-exceptions -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -fobjc-runtime=ios-16.0.0 -x objective-c++ -std=gnu++20 -fexceptions -fcxx-exceptions -fobjc-exceptions -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// An Objective-C @throw in a frame that has a C++ object with a non-trivial
// destructor, and no enclosing @try, must run that destructor on the throw's
// unwind edge: objc_exception_throw is emitted as an `invoke` to a cleanup
// landing pad that calls ~Guard, exactly like the classic frontend. Previously
// ClangIR emitted a non-unwinding `call` + `unreachable` and dropped the
// destructor.

__attribute__((objc_root_class)) @interface Exc @end
@implementation Exc @end

struct Guard { ~Guard(); };

void throw_with_cxx_cleanup(Exc *e) {
  Guard g;
  @throw e;
}

// LLVM-LABEL: define{{.*}} void @_Z22throw_with_cxx_cleanupP3Exc
// LLVM-SAME: personality ptr @__gxx_personality_v0
// LLVM: invoke void @objc_exception_throw(ptr {{.*}})
// LLVM-NEXT: to label %{{.*}} unwind label %[[LPAD:.*]]
// LLVM: [[LPAD]]:
// LLVM: landingpad { ptr, i32 }
// LLVM-NEXT: cleanup
// LLVM: call {{.*}}@_ZN5GuardD1Ev(ptr {{.*}})
// LLVM: resume { ptr, i32 }

// OGCG-LABEL: define{{.*}} void @_Z22throw_with_cxx_cleanupP3Exc
// OGCG-SAME: personality ptr @__gxx_personality_v0
// OGCG: invoke void @objc_exception_throw(ptr {{.*}})
// OGCG-NEXT: to label %{{.*}} unwind label %[[OLPAD:.*]]
// OGCG: [[OLPAD]]:
// OGCG: landingpad { ptr, i32 }
// OGCG-NEXT: cleanup
// OGCG: call {{.*}}@_ZN5GuardD1Ev(ptr {{.*}})
// OGCG: resume { ptr, i32 }
