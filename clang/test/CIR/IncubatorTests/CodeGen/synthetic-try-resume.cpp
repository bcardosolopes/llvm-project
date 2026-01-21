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

// After str is constructed, EH cleanup is active: subsequent calls use synthetic try.
//
// First assignment: str = path (ctor in a scope, no EH cleanup yet)
// CIR: cir.scope {
// CIR:   cir.call @_ZNSbIcEC1EPKcRKNS_9AllocatorE({{.*}}, {{.*}}, {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!s8i>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E3A3AAllocator>) -> ()
// CIR: }
//
// Second assignment: str = path (synthetic try because str's dtor cleanup is active)
// CIR: cir.try synthetic cleanup {
// CIR:   cir.call exception @_ZNSbIcEC1EPKcRKNS_9AllocatorE({{.*}}, {{.*}}, {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!s8i>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E3A3AAllocator>) -> ()
// CIR:   cir.yield
// CIR: } unwind {
// CIR:   cir.resume
// CIR: }
// CIR: cir.try synthetic cleanup {
// CIR:   {{.*}} = cir.call exception @_ZNSbIcEaSERKS_({{.*}}, {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> !cir.ptr<!rec_std3A3Abasic_string3Cchar3E>
// CIR:   cir.yield
// CIR: } unwind {
// CIR:   cir.resume
// CIR: }
// CIR: cir.call @_ZNSbIcED1Ev({{.*}}) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
//
// Third assignment: str = path
// CIR: cir.try synthetic cleanup {
// CIR:   cir.call exception @_ZNSbIcEC1EPKcRKNS_9AllocatorE({{.*}}, {{.*}}, {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!s8i>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E3A3AAllocator>) -> ()
// CIR:   cir.yield
// CIR: } unwind {
// CIR:   cir.resume
// CIR: }
// CIR: cir.try synthetic cleanup {
// CIR:   {{.*}} = cir.call exception @_ZNSbIcEaSERKS_({{.*}}, {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> !cir.ptr<!rec_std3A3Abasic_string3Cchar3E>
// CIR:   cir.yield
// CIR: } unwind {
// CIR:   cir.resume
// CIR: }
// CIR: cir.call @_ZNSbIcED1Ev({{.*}}) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
//
// Final destructor for str
// CIR: cir.call @_ZNSbIcED1Ev({{.*}}) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
// CIR: cir.return

// Calls that may throw lower to invoke/landingpad/resume.
// LLVM: call void @_ZNSbIcEC1EPKcRKNS_9AllocatorE(ptr {{.*}}, ptr {{.*}}, ptr {{.*}})
// LLVM: invoke void @_ZNSbIcEC1EPKcRKNS_9AllocatorE(ptr {{.*}}, ptr {{.*}}, ptr {{.*}})
// LLVM:   to label %{{.*}} unwind label %{{.*}}
// LLVM: {{.*}} = invoke ptr @_ZNSbIcEaSERKS_(ptr {{.*}}, ptr {{.*}})
// LLVM:   to label %{{.*}} unwind label %{{.*}}
// LLVM: call void @_ZNSbIcED1Ev(ptr {{.*}})
// LLVM: invoke void @_ZNSbIcEC1EPKcRKNS_9AllocatorE(ptr {{.*}}, ptr {{.*}}, ptr {{.*}})
// LLVM:   to label %{{.*}} unwind label %{{.*}}
// LLVM: {{.*}} = invoke ptr @_ZNSbIcEaSERKS_(ptr {{.*}}, ptr {{.*}})
// LLVM:   to label %{{.*}} unwind label %{{.*}}
// LLVM: call void @_ZNSbIcED1Ev(ptr {{.*}})
// LLVM: call void @_ZNSbIcED1Ev(ptr {{.*}})
// LLVM: ret void
