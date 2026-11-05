// RUN: %clang_cc1 -std=c++03 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++03 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++03 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test that pre-C++17 throw() uses the same filter-based EH path as OGCG.
// EST_DynamicNone must lower through a zero-filter landingpad plus
// __cxa_call_unexpected, not the terminate helper path.

void foo();

struct allocator {
  ~allocator() throw() { foo(); }
};

allocator a;

// CIR-LABEL: cir.func no_inline optnone comdat linkonce_odr @_ZN9allocatorD2Ev(
// CIR: cir.try synthetic {
// CIR:   cir.call exception @_Z3foov()
// CIR: } catch all {
// CIR:   %[[EXN:.*]] = cir.catch_param : !cir.ptr<!void> {cir.filter_handler}
// CIR:   cir.call @__cxa_call_unexpected(%[[EXN]]) nothrow
// CIR:   cir.unreachable
// CIR: } {cir.filter_types = []}

// Both should emit the destructor with personality, invoke foo(), and use
// __cxa_call_unexpected for the throw() EH spec.
// LLVM: declare void @__cxa_call_unexpected(ptr)
// LLVM: define linkonce_odr void @_ZN9allocatorD2Ev(ptr noundef nonnull align 1 dereferenceable(1) %{{.*}})
// LLVM-SAME: personality ptr @__gxx_personality_v0
// LLVM:   invoke void @_Z3foov()
// LLVM:   landingpad { ptr, i32 }
// LLVM:       filter [0 x ptr] zeroinitializer
// LLVM-NOT:   catch ptr null
// LLVM-NOT:   call void @__clang_call_terminate
// LLVM-NOT:   icmp slt i32
// LLVM-NOT:   resume { ptr, i32 }
// LLVM:   call void @__cxa_call_unexpected

// OGCG: define linkonce_odr void @_ZN9allocatorD2Ev(ptr noundef
// OGCG-SAME: personality ptr @__gxx_personality_v0
// OGCG:   invoke void @_Z3foov()
// OGCG:   landingpad { ptr, i32 }
// OGCG:       filter [0 x ptr] zeroinitializer
// OGCG-NOT:   catch ptr null
// OGCG-NOT:   call void @__clang_call_terminate
// OGCG-NOT:   icmp slt i32
// OGCG-NOT:   resume { ptr, i32 }
// OGCG:   call void @__cxa_call_unexpected
// OGCG: declare void @__cxa_call_unexpected(ptr)
