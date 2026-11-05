// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -I%S/../Inputs -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -I%S/../Inputs -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

#include "std-cxx.h"

// CIR-LABEL:  @_Z3fooPKc
// LLVM-LABEL: @_Z3fooPKc

void foo(const char* path) {
  std::string str = path;
  str = path;
  str = path;
}

// Once str is constructed its destructor is an active EH cleanup, and the calls
// it guards go into a cir.cleanup.scope that gives them one shared unwind edge
// instead of a synthetic try (and a landing pad) each. Each temporary string
// opens a nested scope of its own.
//
// A guarded range stops at the load of a discarded assignment result: that
// value is dead when the cleanup pops, so it has to be assumed to be one CIRGen
// is still holding, and a load cannot be lifted above the range either, since a
// call in it may write what it reads. So each assignment gets its own scope
// rather than all three sharing one, and the third one's temporary constructor,
// sitting between two scopes, keeps its per-call synthetic try.
//
// CIR: %[[STR:.*]] = cir.alloca !rec_std3A3Abasic_string3Cchar3E, {{.*}} ["str", init]
//
// First assignment: str = path (ctor in a scope, no EH cleanup yet)
// CIR: cir.scope {
// CIR:   cir.call @_ZNSbIcEC1EPKcRKNS_9AllocatorE(%[[STR]], {{.*}}, {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!s8i>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E3A3AAllocator>) -> ()
// CIR: }
//
// Second assignment: str = path. The scope's shared cleanup destroys str on any
// unwind out of its body.
// CIR: cir.cleanup.scope {
// CIR:   cir.call exception @_ZNSbIcEC1EPKcRKNS_9AllocatorE(%[[T1:.*]], {{.*}}, {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!s8i>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E3A3AAllocator>) -> ()
// CIR:   cir.cleanup.scope {
// CIR:     {{.*}} = cir.call exception @_ZNSbIcEaSERKS_(%[[STR]], %[[T1]]) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> !cir.ptr<!rec_std3A3Abasic_string3Cchar3E>
// CIR:     cir.yield
// CIR:   } cleanup {
// CIR:     cir.call @_ZNSbIcED1Ev(%[[T1]]) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
// CIR:     cir.yield
// CIR:   }
// CIR:   cir.yield
// CIR: } cleanup {
// CIR:   cir.call @_ZNSbIcED1Ev(%[[STR]]) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
// CIR:   cir.yield
// CIR: }
//
// The dead load that ended the range, then the temporary it outlived -- both
// left outside the scope.
// CIR: cir.load {{.*}} : !cir.ptr<!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E>
// CIR: cir.call @_ZNSbIcED1Ev(%[[T1]]) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
//
// Third assignment: its temporary's constructor is the one call left outside a
// scope, so it still carries its own try, whose cleanup destroys str.
// CIR: cir.try synthetic cleanup {
// CIR:   cir.call exception @_ZNSbIcEC1EPKcRKNS_9AllocatorE(%[[T2:.*]], {{.*}}, {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!s8i>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E3A3AAllocator>) -> () cleanup {
// CIR:     cir.call @_ZNSbIcED1Ev(%[[STR]]) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
// CIR:     cir.yield
// CIR:   }
// CIR:   cir.yield
// CIR: } unwind {
// CIR:   cir.resume
// CIR: }
//
// The assignment itself is guarded by a scope for the temporary. Unwinding out
// of it destroys the temporary and then falls through to str's cleanup, which
// the enclosing EH stack still supplies.
// CIR: cir.cleanup.scope {
// CIR:   {{.*}} = cir.call exception @_ZNSbIcEaSERKS_(%[[STR]], %[[T2]]) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> !cir.ptr<!rec_std3A3Abasic_string3Cchar3E>
// CIR:   cir.yield
// CIR: } cleanup {
// CIR:   cir.call @_ZNSbIcED1Ev(%[[T2]]) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
// CIR:   cir.yield
// CIR: }
//
// Normal-path destructors: the last temporary, then str.
// CIR: cir.load
// CIR: cir.call @_ZNSbIcED1Ev(%[[T2]]) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
// CIR: cir.call @_ZNSbIcED1Ev(%[[STR]]) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
// CIR: cir.return

// Calls that may throw lower to invoke/landingpad/resume.
// LLVM: call void @_ZNSbIcEC1EPKcRKNS_9AllocatorE(ptr {{.*}}, ptr {{.*}}, ptr {{.*}})
// LLVM: invoke void @_ZNSbIcEC1EPKcRKNS_9AllocatorE(ptr {{.*}}, ptr {{.*}}, ptr {{.*}})
// LLVM:   to label %{{.*}} unwind label %{{.*}}
// LLVM: {{.*}} = invoke noundef nonnull align 1 dereferenceable(1) ptr @_ZNSbIcEaSERKS_(ptr noundef {{.*}}, ptr noundef {{.*}})
// LLVM:   to label %{{.*}} unwind label %{{.*}}
// LLVM: call void @_ZNSbIcED1Ev(ptr {{.*}})
// LLVM: invoke void @_ZNSbIcEC1EPKcRKNS_9AllocatorE(ptr {{.*}}, ptr {{.*}}, ptr {{.*}})
// LLVM:   to label %{{.*}} unwind label %{{.*}}
// LLVM: {{.*}} = invoke noundef nonnull align 1 dereferenceable(1) ptr @_ZNSbIcEaSERKS_(ptr {{.*}}, ptr {{.*}})
// LLVM:   to label %{{.*}} unwind label %{{.*}}
// LLVM: call void @_ZNSbIcED1Ev(ptr {{.*}})
// LLVM: call void @_ZNSbIcED1Ev(ptr {{.*}})
// LLVM: ret void
