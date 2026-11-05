// Full pipeline (includes CoroSplit + the module verifier): the module must be
// valid. Before the fix this aborted with "Instruction does not dominate all
// uses! ... call void @_ZdlPvm" / "Broken module found, compilation aborted!".
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll \
// RUN:   -fcxx-exceptions -fexceptions -fsanitize=function
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
//
// Pre-CoroSplit lowering output (the direct-to-LLVM lowering that this fix
// changes), so the synthesized end-catch cleanup landing pad is still present
// and its shape can be checked.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-nopass.ll \
// RUN:   -fcxx-exceptions -fexceptions -fsanitize=function
// RUN: FileCheck %s --input-file=%t-nopass.ll --check-prefix=NOPASS

// Under -fsanitize=function, the function-type CFI check instruments the
// coroutine's coro.size callee, inserting a UBSan check diamond between the
// coroutine-frame reload and operator delete. When the direct-to-LLVM lowering
// synthesizes the __cxa_end_catch-unwind coroutine cleanup landing pad, it must
// rematerialize the whole frame-dealloc region (coro.free, the frame reload, the
// sanitizer diamond, coro.size and operator delete) so the pointer feeding
// operator delete is defined locally in the cleanup and dominates its use.
// Previously only the tail (coro.size + delete) was cloned, leaving the delete
// referencing the frame reload from the normal-return path, which produced a
// "Instruction does not dominate all uses!" broken module. The frame must still
// be freed on this path (no leak), matching classic CodeGen's
// CallCoroDelete::Emit which re-emits the entire `if (coro.free) delete`.

namespace std {
template <typename T, typename... Args> struct coroutine_traits;
template <typename Promise = void> struct coroutine_handle;
template <> struct coroutine_handle<void> {
  static coroutine_handle from_address(void *addr) noexcept { return {}; }
  void *address() const noexcept { return nullptr; }
};
template <typename Promise>
struct coroutine_handle : coroutine_handle<void> {
  static coroutine_handle from_address(void *addr) noexcept { return {}; }
};
struct suspend_never {
  bool await_ready() const noexcept { return true; }
  void await_suspend(coroutine_handle<>) const noexcept {}
  void await_resume() const noexcept {}
};
}

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};
namespace std {
template <> struct coroutine_traits<Task> {
  using promise_type = Task::promise_type;
};
}

// The module verifies (it exists at all only because compilation succeeded), and
// the coroutine frame is still freed via operator delete.
// LLVM-LABEL: define dso_local void @_Z4corov()
// LLVM:       call void @_ZdlPvm(

// The synthesized end-catch cleanup lands here, ends the coroutine on the unwind
// path, and (when in the ramp) frees the frame. (An end_catch invoke unwinds to
// this landing pad; there is another end_catch for the terminate path, so anchor
// on the landing-pad block itself rather than the invoke line.)
// NOPASS-LABEL: define dso_local void @_Z4corov()
// NOPASS:       unwind label %lpad.endcatch.coro
//
// NOPASS:       lpad.endcatch.coro:
// NOPASS:         call void @llvm.coro.end(ptr null, i1 true, token none)
// NOPASS:         %[[INRAMP:.*]] = call i1 @llvm.coro.is_in_ramp()
//
// The cloned guard recomputes coro.free and stores it into the frame slot, so
// the frame reload later in the cloned region is locally defined.
// NOPASS:       cleanup.cont.endcatch.coro:
// NOPASS:         %[[FREE:.*]] = call ptr @llvm.coro.free(token %{{.*}}, ptr %{{.*}})
// NOPASS:         store ptr %[[FREE]], ptr %[[SLOT:.*]], align 8
//
// The frame reload and operator delete are both in the cloned cleanup region,
// so the pointer feeding @_ZdlPvm is the local reload and dominates the delete.
// NOPASS:         %[[FRAME:.*]] = load ptr, ptr %[[SLOT]], align 8
// NOPASS:         call void @_ZdlPvm(ptr noundef %[[FRAME]], i64 noundef %{{.*}})

Task coro() {
  int local = 0;
  (void)local;
  co_return;
}
