// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -fexceptions -fcxx-exceptions -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -fexceptions -fcxx-exceptions -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// When multiple RAII objects are in scope and a call throws, ALL active
// destructors must run during stack unwinding — not just the innermost.

struct Guard {
    Guard();
    ~Guard();
};

void may_throw();

// Single RAII: cleanup destroys the one object.
void test_single() {
    Guard g;
    may_throw();
}

// The guarded range goes in a cir.cleanup.scope whose cleanup region holds the
// destructor, so every throwing call in the range shares one landing pad.
// CIR-LABEL: @_Z11test_singlev
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_Z9may_throwv() : () -> ()
// CIR:     cir.yield
// CIR:   } cleanup {
// CIR:     cir.call @_ZN5GuardD1Ev(%{{[0-9]+}}) nothrow
// CIR:     cir.yield
// CIR:   }

// LLVM-LABEL: @_Z11test_singlev
// LLVM:   invoke void @_Z9may_throwv()
// LLVM:   landingpad { ptr, i32 }
// LLVM:     cleanup
// LLVM:   call void @_ZN5GuardD1Ev
// LLVM:   resume

// OGCG-LABEL: @_Z11test_singlev
// OGCG:   invoke void @_Z9may_throwv()
// OGCG:   landingpad { ptr, i32 }
// OGCG:     cleanup
// OGCG:   call void @_ZN5GuardD1Ev
// OGCG:   resume

// Multiple RAII: cleanup chains through ALL objects (c, b, a).
void test_multiple() {
    Guard a, b, c;
    may_throw();
}

// CIR-LABEL: @_Z13test_multiplev
//
// One cir.cleanup.scope per live object, nested: the innermost guards
// may_throw() and destroys c, the next destroys b, the outermost destroys a.
// Unwinding runs them innermost-first, so the effective order is c, b, a.
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZN5GuardC1Ev(%[[B:[0-9]+]])
// CIR:     cir.cleanup.scope {
// CIR:       cir.call exception @_ZN5GuardC1Ev(%[[C:[0-9]+]])
// CIR:       cir.cleanup.scope {
// CIR:         cir.call exception @_Z9may_throwv() : () -> ()
// CIR:         cir.yield
// CIR:       } cleanup {
// CIR:         cir.call @_ZN5GuardD1Ev(%[[C]]) nothrow
// CIR:         cir.yield
// CIR:       }
// CIR:     } cleanup {
// CIR:       cir.call @_ZN5GuardD1Ev(%[[B]]) nothrow
// CIR:       cir.yield
// CIR:     }
// CIR:   } cleanup {
// CIR:     cir.call @_ZN5GuardD1Ev(%[[A:[0-9]+]]) nothrow
// CIR:     cir.yield
// CIR:   }

// One landing pad per EH scope depth (not per invoke), chained so that a throw
// from may_throw() runs c, b, a while a throw from b's constructor runs only a.
// LLVM-LABEL: @_Z13test_multiplev
// LLVM:   %[[LA:[0-9]+]] = alloca %struct.Guard
// LLVM:   %[[LB:[0-9]+]] = alloca %struct.Guard
// LLVM:   %[[LC:[0-9]+]] = alloca %struct.Guard
// LLVM:   landingpad { ptr, i32 }
// LLVM:   call void @_ZN5GuardD1Ev(ptr {{[^%]*}}%[[LC]])
// LLVM:   landingpad { ptr, i32 }
// LLVM:   call void @_ZN5GuardD1Ev(ptr {{[^%]*}}%[[LB]])
// LLVM:   landingpad { ptr, i32 }
// LLVM:   call void @_ZN5GuardD1Ev(ptr {{[^%]*}}%[[LA]])
// LLVM:   resume

// OGCG chains landing pads via branches (semantically equivalent):
// OGCG-LABEL: @_Z13test_multiplev
// OGCG:   invoke void @_Z9may_throwv()
// OGCG:   landingpad { ptr, i32 }
// OGCG:     cleanup
// OGCG:   call void @_ZN5GuardD1Ev
// OGCG:   call void @_ZN5GuardD1Ev
// OGCG:   call void @_ZN5GuardD1Ev
// OGCG:   resume

// Progressive construction: each constructor's cleanup includes all
// previously-constructed objects.
void test_progressive() {
    Guard a;
    Guard b;
    Guard c;
    may_throw();
}

// Separate declarations produce exactly the same nesting as `Guard a, b, c;`:
// one scope per object, each guarding the rest of the enclosing scope.
// CIR-LABEL: @_Z16test_progressivev
// CIR:   cir.call @_ZN5GuardC1Ev(%[[PA:[0-9]+]])
// CIR:   cir.cleanup.scope {
// CIR:     cir.call exception @_ZN5GuardC1Ev(%[[PB:[0-9]+]])
// CIR:     cir.cleanup.scope {
// CIR:       cir.call exception @_ZN5GuardC1Ev(%[[PC:[0-9]+]])
// CIR:       cir.cleanup.scope {
// CIR:         cir.call exception @_Z9may_throwv() : () -> ()
// CIR:         cir.yield
// CIR:       } cleanup {
// CIR:         cir.call @_ZN5GuardD1Ev(%[[PC]]) nothrow
// CIR:         cir.yield
// CIR:       }
// CIR:     } cleanup {
// CIR:       cir.call @_ZN5GuardD1Ev(%[[PB]]) nothrow
// CIR:       cir.yield
// CIR:     }
// CIR:   } cleanup {
// CIR:     cir.call @_ZN5GuardD1Ev(%[[PA]]) nothrow
// CIR:     cir.yield
// CIR:   }

// LLVM-LABEL: @_Z16test_progressivev
// Each nested scope contributes one landingpad root, and the roots feed a
// shared suffix chain: c's cleanup falls through to b's, which falls through
// to a's, which resumes. Constructing b can only reach a's cleanup.
// LLVM:   invoke void @_ZN5GuardC1Ev(ptr {{[^%]*}}%[[PB2:[0-9]+]])
// LLVM-NEXT: to label %{{.*}} unwind label %[[ROOT_A:[0-9]+]]
// LLVM:   invoke void @_ZN5GuardC1Ev(ptr {{[^%]*}}%[[PC2:[0-9]+]])
// LLVM-NEXT: to label %{{.*}} unwind label %[[ROOT_B:[0-9]+]]
// LLVM:   invoke void @_Z9may_throwv()
// LLVM-NEXT: to label %{{.*}} unwind label %[[ROOT_C:[0-9]+]]
// LLVM: [[ROOT_C]]:
// LLVM-NEXT:   %{{.*}} = landingpad { ptr, i32 }
// LLVM-NEXT:     cleanup
// LLVM-NEXT:   %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 0
// LLVM-NEXT:   %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 1
// LLVM-NEXT:   br label %[[CLEANUP_C:[0-9]+]]
// LLVM: [[CLEANUP_C]]:
// LLVM-NEXT:   %{{.*}} = phi ptr
// LLVM-NEXT:   %{{.*}} = phi i32
// LLVM-NEXT:   call void @_ZN5GuardD1Ev(ptr {{[^%]*}}%[[PC2]])
// LLVM-NEXT:   br label %[[CLEANUP_B:[0-9]+]]
// LLVM: [[ROOT_B]]:
// LLVM-NEXT:   %{{.*}} = landingpad { ptr, i32 }
// LLVM-NEXT:     cleanup
// LLVM-NEXT:   %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 0
// LLVM-NEXT:   %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 1
// LLVM-NEXT:   br label %[[CLEANUP_B]]
// LLVM: [[CLEANUP_B]]:
// LLVM-NEXT:   %{{.*}} = phi ptr
// LLVM-NEXT:   %{{.*}} = phi i32
// LLVM-NEXT:   call void @_ZN5GuardD1Ev(ptr {{[^%]*}}%[[PB2]])
// LLVM-NEXT:   br label %[[CLEANUP_A:[0-9]+]]
// LLVM: [[ROOT_A]]:
// LLVM-NEXT:   %{{.*}} = landingpad { ptr, i32 }
// LLVM-NEXT:     cleanup
// LLVM-NEXT:   %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 0
// LLVM-NEXT:   %{{.*}} = extractvalue { ptr, i32 } %{{.*}}, 1
// LLVM-NEXT:   br label %[[CLEANUP_A]]
// LLVM: [[CLEANUP_A]]:
// LLVM-NEXT:   %{{.*}} = phi ptr
// LLVM-NEXT:   %{{.*}} = phi i32
// LLVM-NEXT:   call void @_ZN5GuardD1Ev(ptr {{[^%]*}}%{{[0-9]+}})
// LLVM-NEXT:   %{{.*}} = insertvalue { ptr, i32 } poison, ptr %{{.*}}, 0
// LLVM-NEXT:   %{{.*}} = insertvalue { ptr, i32 } %{{.*}}, i32 %{{.*}}, 1
// LLVM-NEXT:   resume { ptr, i32 } %{{.*}}

// OGCG-LABEL: @_Z16test_progressivev
// OGCG:   invoke void @_Z9may_throwv()
// OGCG:   landingpad { ptr, i32 }
// OGCG:     cleanup
// OGCG:   call void @_ZN5GuardD1Ev
// OGCG:   call void @_ZN5GuardD1Ev
// OGCG:   call void @_ZN5GuardD1Ev
// OGCG:   resume
