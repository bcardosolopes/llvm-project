// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -std=c++17 -fclangir \
// RUN:   -fclangir-mem2reg -emit-obj -Oz -gkey-instructions \
// RUN:   -debug-info-kind=constructor -dwarf-version=4 -debugger-tuning=lldb \
// RUN:   %s -o /dev/null

// Direct-CIR used to leave !llvm.loop metadata
// attached to a stale file-scoped lambda DISubprogram after the later lambda
// operator() debug-info repair rewrote the function body to the proper
// class-scoped operator() subprogram. The backend verifier then rejected the
// module with "!dbg attachment points at wrong subprogram for function".

namespace fibers {
template <class F> void runInMainContext(F f) { f(); }
}

struct Func {
  int *p;
  void operator()() const { ++*p; }
};

struct Executor {
  virtual void add(Func) = 0;
};

struct WaitExecutor final : Executor {
  Func funcs[4];

  void add(Func) override {}

  int drive() {
    int sum = 0;
    for (int i = 0; i < 4; ++i)
      funcs[i] = Func{&sum};

    fibers::runInMainContext([&]() {
      for (auto &func : funcs)
        func();
    });
    return sum;
  }
};

int driveWaitExecutor() {
  WaitExecutor executor;
  return executor.drive();
}
