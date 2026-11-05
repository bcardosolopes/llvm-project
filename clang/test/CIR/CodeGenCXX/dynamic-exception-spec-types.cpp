// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++14 -fclangir \
// RUN:   -fcxx-exceptions -fexceptions -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll %s

// Dynamic exception specifications (throw(Type...)) are implemented via
// EHFilterScope + __cxa_call_unexpected. The function body is wrapped in
// an implicit landing pad with a filter clause.

// CHECK-LABEL: define dso_local void @_Z3foov()
// CHECK-SAME: personality ptr @__gxx_personality_v0
// CHECK:   invoke void @__cxa_throw
// CHECK: landingpad { ptr, i32 }
// CHECK:     filter [1 x ptr] [ptr @_ZTIi]
// CHECK:   icmp slt i32 %{{.*}}, 0
// CHECK:   call void @__cxa_call_unexpected
// CHECK:   unreachable
// CHECK:   resume { ptr, i32 }
void foo() throw(int) {
  throw 42;
}

// CHECK-LABEL: define dso_local void @_Z3barv()
// CHECK-SAME: personality ptr @__gxx_personality_v0
// CHECK:   invoke void @_Z3foov()
// CHECK: landingpad { ptr, i32 }
// CHECK:     filter [2 x ptr] [ptr @_ZTIi, ptr @_ZTId]
// CHECK:   call void @__cxa_call_unexpected
void bar() throw(int, double) {
  foo();
}
