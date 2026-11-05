// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -fclangir -std=gnu++20 -fcxx-exceptions -fexceptions -emit-cir %s -o /dev/null

// Verify that pointer type mismatches between call arguments and callee
// parameter types (caused by mutable record type member replacement after
// function declarations are emitted) do not crash the CIR verifier.
//
// This reproduces a pattern from unifex::detail::vtable_entry where a
// constexpr constructor takes a function pointer whose type depends on an
// incomplete record. The function pointer placeholder type (func<()>) may
// persist in the callee's declared parameter type while the call argument
// obtains the fully resolved type after record completion.

template <typename Sig> struct vtable_entry;

template <typename Ret, typename... Args>
struct vtable_entry<Ret(Args...)> {
  using fn_t = Ret (*)(Args...);

  template <typename T>
  static constexpr vtable_entry create() noexcept {
    return vtable_entry{static_cast<fn_t>(nullptr)};
  }

  constexpr fn_t get() const noexcept { return fn_; }

private:
  explicit constexpr vtable_entry(fn_t fn) noexcept : fn_(fn) {}
  fn_t fn_;
};

struct Incomplete;

// Force the function pointer type to depend on a forward-declared type.
using entry_type = vtable_entry<void(Incomplete*, void*)>;

// Complete the type.
struct Incomplete { int x; };

// Trigger instantiation after the type is complete.
entry_type make() {
  return entry_type::create<int>();
}
