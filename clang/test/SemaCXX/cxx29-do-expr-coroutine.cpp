// RUN: %clang_cc1 -std=c++2d -fcxx-exceptions -fexceptions -verify -fsyntax-only %s

// expected-no-diagnostics

// A do-expression body is transparent to enclosing function scope, so
// coroutine keywords (co_return, co_yield, co_await) inside the body act
// on the enclosing coroutine — exactly like return/break/continue do.

#include "Inputs/std-coroutine.h"

namespace co_return_inside {
  struct Promise;
  struct Task : std::coroutine_handle<> { using promise_type = Promise; };
  struct Promise {
    Task get_return_object();
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_value(int);
    void unhandled_exception();
  };

  // co_return inside a do-expression body returns from the enclosing
  // coroutine, just like a plain `return` would in a non-coroutine.
  Task f(bool b) {
    int x = do {
      if (!b) co_return -1;     // exits the coroutine
      do_return 1;
    };
    co_return x + 100;
  }
}

namespace co_yield_inside {
  struct Promise;
  struct Generator : std::coroutine_handle<> { using promise_type = Promise; };
  struct Promise {
    Generator get_return_object();
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    std::suspend_always yield_value(int);
    void return_void();
    void unhandled_exception();
  };

  // co_yield inside a do-expression body suspends the enclosing coroutine.
  Generator gen(int n) {
    for (int i = 0; i < n; ++i) {
      int v = do {
        if (i & 1) co_yield i;  // yields from the coroutine
        do_return i * 2;
      };
      (void)v;
    }
    co_return;
  }
}

namespace co_await_inside {
  struct Awaitable {
    bool await_ready();
    void await_suspend(std::coroutine_handle<>);
    int await_resume();
  };

  struct Promise;
  struct Task : std::coroutine_handle<> { using promise_type = Promise; };
  struct Promise {
    Task get_return_object();
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };

  // co_await inside a do-expression body awaits in the enclosing coroutine.
  Task f(Awaitable a) {
    int v = do {
      do_return co_await a;  // resumes with the awaited int
    };
    (void)v;
    co_return;
  }
}

namespace mixed {
  struct Promise;
  struct Generator : std::coroutine_handle<> { using promise_type = Promise; };
  struct Promise {
    Generator get_return_object();
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    std::suspend_always yield_value(int);
    void return_void();
    void unhandled_exception();
  };

  struct Awaitable {
    bool await_ready();
    void await_suspend(std::coroutine_handle<>);
    int await_resume();
  };

  // All three coroutine keywords plus do_return/return/break, all valid
  // because the do-expression is transparent at the enclosing-function
  // level.
  Generator everything(Awaitable a, bool stop) {
    while (true) {
      int v = do {
        if (stop) co_return;   // exits the coroutine entirely
        if (stop) break;       // exits the while loop (compile-only branch)
        co_yield 1;            // yields, then continues in the body
        do_return co_await a;  // yields the do-expression result
      };
      (void)v;
    }
  }
}
