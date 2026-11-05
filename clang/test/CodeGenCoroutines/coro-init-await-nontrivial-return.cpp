// RUN: %clang_cc1 -std=c++20 -triple=x86_64-- -emit-llvm -fcxx-exceptions \
// RUN:            -disable-llvm-passes %s -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -std=c++20 -triple=x86_64-- -emit-llvm -fcxx-exceptions \
// RUN:            -disable-llvm-passes -fclangir %s -o - | FileCheck %s --check-prefix=CIR %}
// RUN: %if cir-support %{ %clang_cc1 -std=c++20 -triple=x86_64-- -emit-llvm -fcxx-exceptions -fexceptions \
// RUN:            -disable-llvm-passes -fclangir %s -o - | FileCheck %s --check-prefix=CIR-FEXN %}

#include "Inputs/coroutine.h"

struct NontrivialType {
  ~NontrivialType() {}
};

struct NontrivialTypeWithThrowingDtor {
  ~NontrivialTypeWithThrowingDtor() noexcept(false) {}
};

namespace can_throw {
struct Task {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct initial_suspend_awaiter {
        bool await_ready() {
            return false;
        }

        void await_suspend(handle_type h) {}

        NontrivialType await_resume() { return {}; }
    };

    struct promise_type {
        void return_void() {}
        void unhandled_exception() {}
        initial_suspend_awaiter initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }
    };

    handle_type handler;
};

Task coro_create() {
    co_return;
}

// CHECK-LABEL: define{{.*}} ptr @_ZN9can_throw11coro_createEv(
// CHECK: init.ready:
// CHECK-NEXT: store i1 true, ptr {{.*}}
// CHECK-NEXT: call void @_ZN9can_throw4Task23initial_suspend_awaiter12await_resumeEv(
// CHECK-NEXT: call void @_ZN14NontrivialTypeD1Ev(
// CHECK-NEXT: store i1 false, ptr {{.*}}
// CIR-LABEL: define dso_local ptr @_ZN9can_throw11coro_createEv(
// CIR: store i{{(1|8)}} 1, ptr {{.*}}
// CIR: call void @_ZN9can_throw4Task23initial_suspend_awaiter12await_resumeEv(
// CIR: call void @_ZN14NontrivialTypeD1Ev(
// CIR: store i{{(1|8)}} 0, ptr {{.*}}
// CIR-FEXN-LABEL: define dso_local ptr @_ZN9can_throw11coro_createEv(
// CIR-FEXN: call ptr @__cxa_begin_catch(
// CIR-FEXN-NEXT: invoke void @_ZN9can_throw4Task12promise_type19unhandled_exceptionEv(
// CIR-FEXN-NEXT: to label %[[INIT_ENDCATCH:[^ ]+]] unwind label %[[INIT_UNHANDLED_LPAD:[^ ]+]]
// CIR-FEXN: [[INIT_ENDCATCH]]:
// CIR-FEXN-NEXT: invoke void @__cxa_end_catch()
// CIR-FEXN-NEXT: to label %[[INIT_CONT:[^ ]+]] unwind label %[[INIT_ENDCATCH_LPAD:[^ ]+]]
// CIR-FEXN: [[INIT_ENDCATCH_LPAD]]:
// CIR-FEXN: call void @llvm.coro.end(ptr null, i1 true, token none)
}

template <typename R>
struct NoexceptResumeTask {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct initial_suspend_awaiter {
        bool await_ready() {
            return false;
        }

        void await_suspend(handle_type h) {}

        R await_resume() noexcept { return {}; }
    };

    struct promise_type {
        void return_void() {}
        void unhandled_exception() {}
        initial_suspend_awaiter initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        NoexceptResumeTask get_return_object() {
            return NoexceptResumeTask{handle_type::from_promise(*this)};
        }
    };

    handle_type handler;
};

namespace no_throw {
using InitNoThrowTask = NoexceptResumeTask<NontrivialType>;

InitNoThrowTask coro_create() {
    co_return;
}

// CHECK-LABEL: define{{.*}} ptr @_ZN8no_throw11coro_createEv(
// CHECK: init.ready:
// CHECK-NEXT: call void @_ZN18NoexceptResumeTaskI14NontrivialTypeE23initial_suspend_awaiter12await_resumeEv(
// CHECK-NEXT: call void @_ZN14NontrivialTypeD1Ev(
// CIR-LABEL: define dso_local ptr @_ZN8no_throw11coro_createEv(
// CIR-NOT: store i{{(1|8)}} 1, ptr {{.*}}
// CIR: switch i8 %{{.*}}, label %{{.*}} [
// CIR: i8 0, label %[[CIR_INIT_READY:[^ ]+]]
// CIR: i8 1, label %[[CIR_INIT_CLEANUP:[^ ]+]]
// CIR: ]
// CIR: [[CIR_INIT_CLEANUP]]:
// CIR-NOT: call void @_ZN14NontrivialTypeD1Ev(
// CIR: br label %[[CIR_DESTROY_COMMON:[^ ]+]]
// CIR: [[CIR_INIT_READY]]:
// CIR: call void @_ZN18NoexceptResumeTaskI14NontrivialTypeE23initial_suspend_awaiter12await_resumeEv(
// CIR: call void @_ZN14NontrivialTypeD1Ev(
}

namespace throwing_dtor {
using InitTaskWithThrowingDtor = NoexceptResumeTask<NontrivialTypeWithThrowingDtor>;

InitTaskWithThrowingDtor coro_create() {
    co_return;
}

// CHECK-LABEL: define{{.*}} ptr @_ZN13throwing_dtor11coro_createEv(
// CHECK: init.ready:
// CHECK-NEXT: store i1 true, ptr {{.*}}
// CHECK-NEXT: call void @_ZN18NoexceptResumeTaskI30NontrivialTypeWithThrowingDtorE23initial_suspend_awaiter12await_resumeEv(
// CHECK-NEXT: call void @_ZN30NontrivialTypeWithThrowingDtorD1Ev(
// CHECK-NEXT: store i1 false, ptr {{.*}}
// CIR-LABEL: define dso_local ptr @_ZN13throwing_dtor11coro_createEv(
// CIR: store i{{(1|8)}} 1, ptr {{.*}}
// CIR: call void @_ZN18NoexceptResumeTaskI30NontrivialTypeWithThrowingDtorE23initial_suspend_awaiter12await_resumeEv(
// CIR: call void @_ZN30NontrivialTypeWithThrowingDtorD1Ev(
// CIR: store i{{(1|8)}} 0, ptr {{.*}}
}
