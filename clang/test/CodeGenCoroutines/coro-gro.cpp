// Verifies lifetime of __gro local variable
// Verify that coroutine promise and allocated memory are freed up on exception.
// RUN: %clang_cc1 -std=c++20 -triple=x86_64-unknown-linux-gnu -emit-llvm -o - %s -disable-llvm-passes | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -std=c++20 -triple=x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR %}
// RUN: %if cir-support %{ %clang_cc1 -std=c++20 -triple=x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -o - %s -disable-llvm-passes | FileCheck %s --check-prefix=CIR-LLVM %}

#include "Inputs/coroutine.h"

using namespace std;

struct GroType {
  ~GroType();
  operator int() noexcept;
};

template <> struct std::coroutine_traits<int> {
  struct promise_type {
    GroType get_return_object() noexcept;
    suspend_always initial_suspend() noexcept;
    suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    promise_type();
    ~promise_type();
    void unhandled_exception() noexcept;
  };
};

struct Cleanup { ~Cleanup(); };
void doSomething() noexcept;

// CHECK: define{{.*}} i32 @_Z1fv(
// CIR-LABEL: cir.func coroutine no_inline optnone dso_local @_Z1fv()
// CIR: %[[CIRPromise:.+]] = cir.alloca !rec_std3A3Acoroutine_traits3Cint3E3A3Apromise_type, !cir.ptr<!rec_std3A3Acoroutine_traits3Cint3E3A3Apromise_type>, ["__promise", init]
// CIR: cir.call exception @_ZNSt16coroutine_traitsIiJEE12promise_typeC1Ev(
// CIR: cir.call @__builtin_coro_free(
// CIR: cir.call @_ZdlPvm(
// CIR-LLVM-LABEL: define dso_local noundef i32 @_Z1fv() {{.*}} personality ptr @__gxx_personality_v0 {
// CIR-LLVM: %[[LLPromise:[0-9]+]] = alloca %"struct.std::coroutine_traits<int>::promise_type", i64 1, align 1
// CIR-LLVM: %[[LLCoroId:[0-9]+]] = call token @llvm.coro.id(i32 16, ptr %[[LLPromise]], ptr null, ptr null)
// CIR-LLVM: %[[LLCoroFrame:[0-9]+]] = call ptr @llvm.coro.begin(token %[[LLCoroId]], ptr %{{[0-9]+}})
// CIR-LLVM: invoke void @_ZNSt16coroutine_traitsIiJEE12promise_typeC1Ev(
// CIR-LLVM: landingpad { ptr, i32 }
// CIR-LLVM: cleanup
// CIR-LLVM: call void @_ZdlPvm(ptr noundef %{{.*}}, i64 noundef %{{.*}})
int f() {
  // CHECK: %[[RetVal:.+]] = alloca i32
  // CHECK-NEXT: %[[GroActive:.+]] = alloca i1
  // CHECK-NEXT: %[[Promise:.+]] = alloca %"struct.std::coroutine_traits<int>::promise_type", align 1
  // CHECK-NEXT: %[[CoroGro:.+]] = alloca %struct.GroType, {{.*}} !coro.outside.frame ![[OutFrameMetadata:.+]]

  // CHECK: %[[Size:.+]] = call i64 @llvm.coro.size.i64()
  // CHECK-NEXT: call noalias noundef nonnull ptr @_Znwm(i64 noundef %[[Size]])

  // CHECK: store i1 false, ptr %[[GroActive]]
  // CHECK-NEXT: call void @llvm.lifetime.start.p0(ptr %[[Promise]])
  // CHECK-NEXT: call void @_ZNSt16coroutine_traitsIiJEE12promise_typeC1Ev(
  // CHECK-NEXT: call void @llvm.lifetime.start.p0(ptr %[[CoroGro]])
  // CHECK-NEXT: call void @_ZNSt16coroutine_traitsIiJEE12promise_type17get_return_objectEv({{.*}} %[[CoroGro]]
  // CHECK-NEXT: store i1 true, ptr %[[GroActive]]

  Cleanup cleanup;
  doSomething();
  co_return;

  // CHECK: call void @_Z11doSomethingv(
  // CHECK-NEXT: call void @_ZNSt16coroutine_traitsIiJEE12promise_type11return_voidEv(
  // CHECK-NEXT: call void @_ZN7CleanupD1Ev(

  // Initialize retval from Gro and destroy Gro
  // Note this also tests delaying initialization when Gro and function return
  // types mismatch (see cwg2563).

  // CHECK: pre.gvo.conv:
  // CHECK-NEXT: %[[IsFinalExit:.+]] = phi i1 [ true, %cleanup8 ], [ false, %final.suspend ], [ false, %init.suspend ]
  // CHECK-NEXT: %InRamp = call i1 @llvm.coro.is_in_ramp()
  // CHECK-NEXT: br i1 %InRamp, label %[[GroConv:.+]], label %[[AfterGroConv:.+]]

  // CHECK: [[GroConv]]:
  // CHECK-NEXT: %[[Conv:.+]] = call noundef i32 @_ZN7GroTypecviEv(
  // CHECK-NEXT: store i32 %[[Conv]], ptr %[[RetVal]]
  // CHECK-NEXT: br label %[[AfterGroConv]]

  // CHECK: [[AfterGroConv]]:
  // CHECK-NEXT: br i1  %[[IsFinalExit]], label %cleanup.cont10, label %[[CoroRet:.+]]

  // CHECK: cleanup.cont10:
  // CHECK-NEXT: br label %[[Cleanup:.+]]

  // CHECK: [[Cleanup]]:
  // CHECK-NEXT: %{{.*}} = phi i32
  // CHECK-NEXT: %[[IsActive:.+]] = load i1, ptr %[[GroActive]]
  // CHECK-NEXT: br i1 %[[IsActive]], label %[[CleanupGro:.+]], label %[[Done:.+]]

  // CHECK: [[CleanupGro]]:
  // CHECK-NEXT: call void @_ZN7GroTypeD1Ev(
  // CHECK-NEXT: br label %[[Done]]

  // Destroy promise and free the memory.

  // CHECK: [[Done]]:
  // CHECK-NEXT: call void @llvm.lifetime.end.p0(ptr %[[CoroGro]])
  // CHECK-NEXT: call void @_ZNSt16coroutine_traitsIiJEE12promise_typeD1Ev(
  // CHECK-NEXT: call void @llvm.lifetime.end.p0(ptr %[[Promise]])
  // CHECK-NEXT: %[[Mem:.+]] = call ptr @llvm.coro.free(

  // On the fallthrough-completion path the is_in_ramp-guarded delayed
  // get-return-object conversion runs before the GRO temporary and the promise
  // are destroyed, and between final suspend and the frame free each of them is
  // destroyed exactly once. The -NOT directives pin both halves: the bug this
  // pins double-destroyed the GRO temporary as well as the promise.
  // CIR: cir.call @_ZNSt16coroutine_traitsIiJEE12promise_type13final_suspendEv(%[[CIRPromise]])
  // CIR-NOT: cir.call @_ZNSt16coroutine_traitsIiJEE12promise_typeD1Ev(
  // CIR-NOT: cir.call @_ZN7GroTypeD1Ev(
  // CIR: cir.lifetime.end %{{.+}} : !cir.ptr<!rec_std3A3Asuspend_always>
  // CIR-NOT: cir.call @_ZNSt16coroutine_traitsIiJEE12promise_typeD1Ev(
  // CIR-NOT: cir.call @_ZN7GroTypeD1Ev(
  // CIR: cir.label "_Z1fv.__coro_destroy"
  // CIR-NEXT: %[[CIRInRamp:.+]] = cir.call @__builtin_coro_is_in_ramp() : () -> !cir.bool
  // CIR-NEXT: cir.if %[[CIRInRamp]] {
  // CIR-NEXT: %[[CIRConv:.+]] = cir.call @_ZN7GroTypecviEv(%[[CIRGro:[0-9]+]]) nothrow
  // CIR-NEXT: cir.store align(4) %[[CIRConv]], %[[CIRRet:[0-9]+]] : !s32i, !cir.ptr<!s32i>
  // CIR-NEXT: } {cir.coro_gro_conv}
  // CIR-NEXT: cir.call @_ZN7GroTypeD1Ev(%[[CIRGro]]) nothrow
  // CIR-NEXT: cir.lifetime.end %[[CIRGro]] : !cir.ptr<!rec_GroType>
  // CIR-NEXT: cir.call @_ZNSt16coroutine_traitsIiJEE12promise_typeD1Ev(%[[CIRPromise]])
  // CIR-NEXT: cir.lifetime.end %[[CIRPromise]] : !cir.ptr<!rec_std3A3Acoroutine_traits3Cint3E3A3Apromise_type>
  // CIR-NOT: cir.call @_ZNSt16coroutine_traitsIiJEE12promise_typeD1Ev(
  // CIR-NOT: cir.call @_ZN7GroTypeD1Ev(
  // CIR-NOT: cir.store {{.*}}, %[[CIRRet]] :
  // CIR: %[[CIRMem:.+]] = cir.call @__builtin_coro_free(
  // CIR-NOT: cir.call @_ZNSt16coroutine_traitsIiJEE12promise_typeD1Ev(
  // CIR-NOT: cir.call @_ZN7GroTypeD1Ev(
  // CIR-NOT: cir.store {{.*}}, %[[CIRRet]] :
  // The slot the conversion wrote is the one `f()` returns, unwritten since.
  // CIR: %[[CIRRetVal:.+]] = cir.load %[[CIRRet]] : !cir.ptr<!s32i>, !s32i
  // CIR-NEXT: cir.return %[[CIRRetVal]]

  // FlattenCFG also clones the guarded conversion onto every suspend-return
  // edge, so walk in from the final suspend to pin the fallthrough copy rather
  // than one of the clones.
  // CIR-LLVM: call void @_ZNSt16coroutine_traitsIiJEE12promise_type13final_suspendEv(ptr{{.*}} %[[LLPromise]])
  // CIR-LLVM-NEXT: call noundef zeroext i1 @_ZNSt14suspend_always11await_readyEv(ptr{{.*}} %[[LLAwaiter:[0-9]+]])
  // CIR-LLVM-NOT: call void @_ZN7GroTypeD1Ev(
  // CIR-LLVM-NOT: call void @_ZNSt16coroutine_traitsIiJEE12promise_typeD1Ev(
  // CIR-LLVM: call void @_ZNSt14suspend_always12await_resumeEv(ptr{{.*}} %[[LLAwaiter]])
  // CIR-LLVM-NEXT: br label %[[LLAwaitEndBB:[0-9]+]]
  // CIR-LLVM: {{^}}[[LLAwaitEndBB]]:
  // CIR-LLVM-NEXT: call void @llvm.lifetime.end.p0(ptr %[[LLAwaiter]])
  // CIR-LLVM-NEXT: br label %[[LLRampBB:[0-9]+]]
  // CIR-LLVM: {{^}}[[LLRampBB]]:
  // CIR-LLVM-NEXT: %[[LLInRamp:[0-9]+]] = call i1 @llvm.coro.is_in_ramp()
  // CIR-LLVM-NEXT: br i1 %[[LLInRamp]], label %[[LLConvBB:[0-9]+]], label %[[LLCleanupBB:[0-9]+]]
  // CIR-LLVM: {{^}}[[LLConvBB]]:
  // CIR-LLVM-NEXT: %[[LLConv:[0-9]+]] = call noundef i32 @_ZN7GroTypecviEv(ptr{{.*}} %[[LLGro:[0-9]+]])
  // CIR-LLVM-NEXT: store i32 %[[LLConv]], ptr %[[LLRet:[0-9]+]], align 4
  // CIR-LLVM-NEXT: br label %[[LLCleanupBB]]
  // CIR-LLVM: {{^}}[[LLCleanupBB]]:
  // CIR-LLVM-NEXT: call void @_ZN7GroTypeD1Ev(ptr{{.*}} %[[LLGro]])
  // CIR-LLVM-NEXT: call void @llvm.lifetime.end.p0(ptr %[[LLGro]])
  // CIR-LLVM-NEXT: call void @_ZNSt16coroutine_traitsIiJEE12promise_typeD1Ev(ptr{{.*}} %[[LLPromise]])
  // CIR-LLVM-NEXT: call void @llvm.lifetime.end.p0(ptr %[[LLPromise]])
  // CIR-LLVM-NOT: call void @_ZNSt16coroutine_traitsIiJEE12promise_typeD1Ev(
  // CIR-LLVM-NOT: call void @_ZN7GroTypeD1Ev(
  // CIR-LLVM-NOT: store i32 {{.*}}, ptr %[[LLRet]],
  // CIR-LLVM: %[[LLMem:[0-9]+]] = call ptr @llvm.coro.free(token %[[LLCoroId]], ptr %[[LLCoroFrame]])
  // CIR-LLVM-NOT: call void @_ZNSt16coroutine_traitsIiJEE12promise_typeD1Ev(
  // CIR-LLVM-NOT: call void @_ZN7GroTypeD1Ev(
  // CIR-LLVM-NOT: store i32 {{.*}}, ptr %[[LLRet]],
  // CIR-LLVM: %[[LLRetVal:[0-9]+]] = load i32, ptr %[[LLRet]], align 4
  // CIR-LLVM-NEXT: ret i32 %[[LLRetVal]]

  // CHECK: %[[SIZE:.+]] = call i64 @llvm.coro.size.i64()
  // CHECK-NEXT: call void @_ZdlPvm(ptr noundef %[[Mem]], i64 noundef %[[SIZE]])

  // CHECK: [[CoroRet]]:
  // CHECK-NEXT: call void @llvm.coro.end(
  // CHECK-NEXT: %[[LoadRet:.+]] = load i32, ptr %[[RetVal]]
  // CHECK-NEXT: ret i32 %[[LoadRet]]
}

class invoker {
public:
  class invoker_promise {
  public:
    invoker get_return_object() { return invoker{}; }
    auto initial_suspend() { return suspend_always{}; }
    auto final_suspend() noexcept { return suspend_always{}; }
    void return_void() {}
    void unhandled_exception() {}
  };
  using promise_type = invoker_promise;
  invoker() {}
  invoker(const invoker &) = delete;
  invoker &operator=(const invoker &) = delete;
  invoker(invoker &&) = delete;
  invoker &operator=(invoker &&) = delete;
};

// According to cwg2563, matching GRO and function return type must allow
// for eager initialization and RVO.
// CHECK: define{{.*}} void @_Z1gv({{.*}} %[[AggRes:.+]])
// CIR-LABEL: cir.func coroutine no_inline optnone dso_local @_Z1gv(
// CIR: cir.call exception @_ZN7invoker15invoker_promise17get_return_objectEv(
// CIR: cir.call @__builtin_coro_free(
// CIR: cir.call @_ZdlPvm(
// CIR-LLVM-LABEL: define dso_local void @_Z1gv({{.*}} personality ptr @__gxx_personality_v0 {
// CIR-LLVM: invoke void @_ZN7invoker15invoker_promise17get_return_objectEv(
// CIR-LLVM: landingpad { ptr, i32 }
// CIR-LLVM: cleanup
// CIR-LLVM: call void @_ZdlPvm(ptr noundef %{{.*}}, i64 noundef %{{.*}})
invoker g() {
  // CHECK: %[[ResultPtr:.+]] = alloca ptr
  // CHECK-NEXT: %[[Promise:.+]] = alloca %"class.invoker::invoker_promise"

  // CHECK: store ptr %[[AggRes]], ptr %[[ResultPtr]]
  // CHECK: coro.init:
  // CHECK: = call ptr @llvm.coro.begin

  // delayed GRO pattern stores a GRO active flag, make sure to not emit it.
  // CHECK-NOT: store i1 false, ptr
  // CHECK: call void @_ZN7invoker15invoker_promise17get_return_objectEv({{.*}} %[[AggRes]]
  co_return;
}

namespace gh148953 {

struct Task {
  struct promise_type {
    Task get_return_object();
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
  Task() {}
  // Different from `invoker`, this Task is copy constructible.
  Task(const Task&) {};
};

// NRVO on const qualified return type should work.
// CHECK: define{{.*}} void @_ZN8gh1489537exampleEv({{.*}} sret(%"struct.gh148953::Task") align 1 %[[NrvoRes:.+]])
const Task example() {
  // CHECK: %[[ResultPtr:.+]] = alloca ptr
  // CHECK: store ptr %[[NrvoRes]], ptr %[[ResultPtr]]
  // CHECK: coro.init:
  // CHECK: call void @_ZN8gh1489534Task12promise_type17get_return_objectEv({{.*}} %[[NrvoRes:.+]], {{.*}})
  co_return;
}

} // namespace gh148953
// CHECK: ![[OutFrameMetadata]] = !{}
