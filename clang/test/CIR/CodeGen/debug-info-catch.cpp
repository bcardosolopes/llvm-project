// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fcxx-exceptions -fexceptions -debug-info-kind=limited \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that exception handling calls have debug locations when compiling
// with debug info. This prevents LLVM verifier errors about inlinable calls
// missing !dbg locations.

void may_throw();
void handle_exception();
struct Guard { ~Guard(); };

// Verify __clang_call_terminate body has debug locations on all calls.
// CHECK-LABEL: define linkonce_odr hidden void @__clang_call_terminate
// CHECK: call ptr @__cxa_begin_catch(ptr {{.*}}){{.*}}, !dbg
// CHECK: call void @_ZSt9terminatev(){{.*}}, !dbg

// Verify the catch_all_test function has debug locations on EH calls.
// CHECK-LABEL: define {{.*}} @_Z14catch_all_testv
// CHECK: call ptr @__cxa_begin_catch(ptr {{.*}}){{.*}}, !dbg
// CHECK: invoke void @__cxa_end_catch()
// CHECK: !dbg
void catch_all_test() {
  Guard g;
  try {
    may_throw();
  } catch (...) {
    handle_exception();
  }
}
