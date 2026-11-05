// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -disable-llvm-passes -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -disable-llvm-passes -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR-EH %}
#include "Inputs/coroutine.h"

using namespace std;

struct coro {
  struct promise_type {
    coro get_return_object();
    suspend_never initial_suspend();
    suspend_never final_suspend() noexcept;
    void return_void();
    static void unhandled_exception();
  };
};

// CHECK: void @_Z3foov() #[[FOO_ATTR_NUM:[0-9]+]]
// CHECK: declare token @llvm.coro.save(ptr) #[[SAVE_ATTR_NUM:[0-9]+]]
// CHECK: void @_Z3foov.__await_suspend_wrapper__init({{.*}}) #[[WRAPPER_ATTR_NUM:[0-9]+]]
// CHECK: void @_Z3foov.__await_suspend_wrapper__await({{.*}}) #[[WRAPPER_ATTR_NUM:[0-9]+]]
// CHECK: void @_Z3foov.__await_suspend_wrapper__final({{.*}}) #[[WRAPPER_ATTR_NUM:[0-9]+]]
// CHECK: attributes #[[FOO_ATTR_NUM]] = { {{.*}} presplitcoroutine
// CHECK: attributes #[[SAVE_ATTR_NUM]] = { {{.*}}nomerge
// CHECK: attributes #[[WRAPPER_ATTR_NUM]] = { {{.*}}"min-legal-vector-width"="0" "sample-profile-suffix-elision-policy"="selected"
// CIR-DAG: define dso_local void @_Z3foov() #[[FOO_ATTR_NUM:[0-9]+]]
// CIR-DAG: define internal void @_Z3foov.__await_suspend_wrapper__init({{.*}}) #[[WRAPPER_ATTR_NUM:[0-9]+]]
// CIR-DAG: define internal void @_Z3foov.__await_suspend_wrapper__await({{.*}}) #[[WRAPPER_ATTR_NUM]]
// CIR-DAG: define internal void @_Z3foov.__await_suspend_wrapper__final({{.*}}) #[[WRAPPER_ATTR_NUM]]
// CIR-DAG: attributes #[[FOO_ATTR_NUM]] = { {{.*}} presplitcoroutine
// CIR-DAG: attributes #[[WRAPPER_ATTR_NUM]] = { {{.*}}"min-legal-vector-width"="0" "sample-profile-suffix-elision-policy"="selected"
// CIR-EH: call void @llvm.coro.end(ptr null, i1 true, token none)
// CIR-EH: call i1 @llvm.coro.is_in_ramp()
// CIR-EH: call void @llvm.coro.end(ptr null, i1 false, token none)
coro foo() {
  co_await suspend_always{};
}
