// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++14 -fclangir \
// RUN:   -fcxx-exceptions -fexceptions -Wno-dynamic-exception-spec -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++14 -fclangir \
// RUN:   -fcxx-exceptions -fexceptions -Wno-dynamic-exception-spec -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Dynamic exception specification throw(Type) is implemented via
// EHFilterScope + __cxa_call_unexpected. CIR emits a synthetic cir.try
// with filter_types attribute, which FlattenCFG lowers to a filter clause
// on the landing pad.

class B { virtual void foo(); };

void opaque();

void test1() throw(B) {
  opaque();
}

// CIR-LABEL: cir.func {{.*}}@_Z5test1v()
// CIR: cir.try synthetic {
// CIR:   cir.call exception @_Z6opaquev()
// CIR: } catch all {
// CIR:   %[[EXN:.*]] = cir.catch_param : !cir.ptr<!void> {cir.filter_handler}
// CIR:   cir.call @__cxa_call_unexpected(%[[EXN]]) nothrow
// CIR:   cir.unreachable
// CIR: } {cir.filter_types =

// LLVM-LABEL: define dso_local void @_Z5test1v()
// LLVM-SAME: personality ptr @__gxx_personality_v0
// LLVM:   invoke void @_Z6opaquev()
// LLVM: landingpad { ptr, i32 }
// LLVM:     filter [1 x ptr] [ptr @_ZTI1B]
// LLVM:   icmp slt i32 %{{.*}}, 0
// LLVM:   call void @__cxa_call_unexpected
// LLVM:   unreachable
// LLVM:   resume { ptr, i32 }
