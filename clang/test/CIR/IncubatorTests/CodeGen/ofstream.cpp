// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -I%S/../Inputs -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -I%S/../Inputs -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

#include "std-cxx.h"

namespace std {
template <class CharT> class basic_ofstream {
public:
  basic_ofstream();
  ~basic_ofstream();
  explicit basic_ofstream(const char *);
};

using ofstream = basic_ofstream<char>;

ofstream &operator<<(ofstream &, const string &);
} // namespace std

void foo(const char *path) {
  std::ofstream fout1(path);
  fout1 << path;
  std::ofstream fout2(path);
  fout2 << path;
}

// CIR: cir.func {{.*}} @_Z3fooPKc
// CIR: %[[V1:.*]] = cir.alloca !rec_std3A3Abasic_ofstream3Cchar3E, !cir.ptr<!rec_std3A3Abasic_ofstream3Cchar3E>, ["fout1", init] {alignment = 1 : i64}
// CIR: %[[V2:.*]] = cir.alloca !rec_std3A3Abasic_ofstream3Cchar3E, !cir.ptr<!rec_std3A3Abasic_ofstream3Cchar3E>, ["fout2", init] {alignment = 1 : i64}
//
// First call (fout1 ctor) - no EH cleanup scope active yet, so plain call.
// CIR: cir.call @_ZNSt14basic_ofstreamIcEC1EPKc(%[[V1]], {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_ofstream3Cchar3E>, !cir.ptr<!s8i>) -> ()
// After fout1 is constructed, EH cleanup is active: subsequent calls use synthetic try.
// CIR: cir.try synthetic cleanup {
// CIR:   cir.call exception @_ZNSbIcEC1EPKcRKNS_9AllocatorE({{.*}}, {{.*}}, {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!s8i>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E3A3AAllocator>) -> ()
// CIR:   cir.yield
// CIR: } unwind {
// CIR:   cir.resume
// CIR: }
// CIR: cir.try synthetic cleanup {
// CIR:   {{.*}} = cir.call exception @_ZStlsRSt14basic_ofstreamIcERKSbIcE(%[[V1]], {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_ofstream3Cchar3E>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> !cir.ptr<!rec_std3A3Abasic_ofstream3Cchar3E>
// CIR:   cir.yield
// CIR: } unwind {
// CIR:   cir.resume
// CIR: }
// CIR: cir.call @_ZNSbIcED1Ev({{.*}}) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
// CIR: cir.try synthetic cleanup {
// CIR:   cir.call exception @_ZNSt14basic_ofstreamIcEC1EPKc(%[[V2]], {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_ofstream3Cchar3E>, !cir.ptr<!s8i>) -> ()
// CIR:   cir.yield
// CIR: } unwind {
// CIR:   cir.resume
// CIR: }
// CIR: cir.try synthetic cleanup {
// CIR:   cir.call exception @_ZNSbIcEC1EPKcRKNS_9AllocatorE({{.*}}, {{.*}}, {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>, !cir.ptr<!s8i>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E3A3AAllocator>) -> ()
// CIR:   cir.yield
// CIR: } unwind {
// CIR:   cir.resume
// CIR: }
// CIR: cir.try synthetic cleanup {
// CIR:   {{.*}} = cir.call exception @_ZStlsRSt14basic_ofstreamIcERKSbIcE(%[[V2]], {{.*}}) : (!cir.ptr<!rec_std3A3Abasic_ofstream3Cchar3E>, !cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> !cir.ptr<!rec_std3A3Abasic_ofstream3Cchar3E>
// CIR:   cir.yield
// CIR: } unwind {
// CIR:   cir.resume
// CIR: }
// CIR: cir.call @_ZNSbIcED1Ev({{.*}}) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_string3Cchar3E>) -> ()
// CIR: cir.call @_ZNSt14basic_ofstreamIcED1Ev(%[[V2]]) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_ofstream3Cchar3E>) -> ()
// CIR: cir.call @_ZNSt14basic_ofstreamIcED1Ev(%[[V1]]) {{.*}}: (!cir.ptr<!rec_std3A3Abasic_ofstream3Cchar3E>) -> ()
// CIR: cir.return

// Calls that may throw lower to invoke/landingpad/resume.
// LLVM: @_Z3fooPKc(ptr {{.*}})
// LLVM:   call void @_ZNSt14basic_ofstreamIcEC1EPKc(ptr {{.*}}, ptr {{.*}})
// LLVM:   invoke void @_ZNSbIcEC1EPKcRKNS_9AllocatorE(ptr {{.*}}, ptr {{.*}}, ptr {{.*}})
// LLVM:     to label %{{.*}} unwind label %{{.*}}
// LLVM:   {{.*}} = invoke ptr @_ZStlsRSt14basic_ofstreamIcERKSbIcE(ptr {{.*}}, ptr {{.*}})
// LLVM:     to label %{{.*}} unwind label %{{.*}}
// LLVM:   call void @_ZNSbIcED1Ev(ptr {{.*}})
// LLVM:   invoke void @_ZNSt14basic_ofstreamIcEC1EPKc(ptr {{.*}}, ptr {{.*}})
// LLVM:     to label %{{.*}} unwind label %{{.*}}
// LLVM:   invoke void @_ZNSbIcEC1EPKcRKNS_9AllocatorE(ptr {{.*}}, ptr {{.*}}, ptr {{.*}})
// LLVM:     to label %{{.*}} unwind label %{{.*}}
// LLVM:   {{.*}} = invoke ptr @_ZStlsRSt14basic_ofstreamIcERKSbIcE(ptr {{.*}}, ptr {{.*}})
// LLVM:     to label %{{.*}} unwind label %{{.*}}
// LLVM:   call void @_ZNSbIcED1Ev(ptr {{.*}})
// LLVM:   call void @_ZNSt14basic_ofstreamIcED1Ev(ptr {{.*}})
// LLVM:   call void @_ZNSt14basic_ofstreamIcED1Ev(ptr {{.*}})
// LLVM:   ret void
