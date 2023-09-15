// RUN: %clang_cc1 -std=c++20 -triple=x86_64-unknown-linux-gnu -fcxx-exceptions -emit-llvm -o %t.ll %s -disable-llvm-passes
// RUN: FileCheck --input-file=%t.ll %s

// Verifies lifetime of __coro_gro variable w.r.t to the promise type,
// more specifically make sure the coroutine frame isn't destroyed *before*
// the conversion function that unwraps the promise proxy as part of
// short-circuiting coroutines delayed behavior.

#include "Inputs/coroutine.h"

using namespace std;

namespace folly {

template <class Error>
struct unexpected final {
  Error error;

  constexpr unexpected() = default;
  constexpr unexpected(unexpected const&) = default;
  unexpected& operator=(unexpected const&) = default;
  constexpr /* implicit */ unexpected(Error err) : error(err) {}
};

template <class Value, class Error>
class expected final {
  enum class Which : unsigned char { empty, value, error };

  Which which_;
  Error error_;
  Value value_;

 public:
  struct promise_type;

  constexpr expected() noexcept : which_(Which::empty) {}
  constexpr expected(expected const& that) = default;

  expected(expected*& pointer) noexcept : which_(Which::empty) {
    pointer = this;
  }

  constexpr /* implicit */ expected(Value val) noexcept
      : which_(Which::value), value_(val) {}

  /* implicit */ expected(Error) = delete;

  constexpr /* implicit */ expected(unexpected<Error> err) noexcept
      : which_(Which::error), error_(err.error) {}

  expected& operator=(expected const& that) = default;

  expected& operator=(Value val) noexcept {
    which_ = Which::value;
    value_ = val;
    return *this;
  }

  expected& operator=(unexpected<Error> err) noexcept {
    which_ = Which::error;
    error_ = err.error;
    return *this;
  }

  /* implicit */ expected& operator=(Error) = delete;

  constexpr bool has_value() const noexcept {
    return Which::value == this->which_;
  }

  constexpr bool has_error() const noexcept {
    return Which::error == this->which_;
  }

  Value value() const {
    require_value();
    return value_;
  }

  Error error() const {
    require_error();
    return error_;
  }

 private:
  void require_value() const {
    if (!has_value()) {
      // terminate
    }
  }

  void require_error() const {
    if (!has_error()) {
      // terminate
    }
  }
};

template <typename Value, typename Error>
struct expected<Value, Error>::promise_type {
  struct return_object;

  expected<Value, Error>* value_ = nullptr;

  promise_type() = default;
  promise_type(promise_type const&) = delete;
  return_object get_return_object() noexcept {
    return return_object{value_};
  }
  std::suspend_never initial_suspend() const noexcept {
    return {};
  }
  std::suspend_never final_suspend() const noexcept {
    return {};
  }
  void return_value(Value u) {
    *value_ = u;
  }
  void unhandled_exception() {
    throw;
  }
};

template <typename Value, typename Error>
struct expected<Value, Error>::promise_type::return_object {
  expected<Value, Error> storage_;
  expected<Value, Error>*& pointer_;

  explicit return_object(expected<Value, Error>*& p) noexcept : pointer_{p} {
    pointer_ = &storage_;
  }
  return_object(return_object const&) = delete;
  ~return_object() {}

  /* implicit */ operator expected<Value, Error>() {
    return storage_; // deferred
  }
};

template <typename Value, typename Error>
struct expected_awaitable {
  using promise_type = typename expected<Value, Error>::promise_type;

  expected<Value, Error> o_;

  explicit expected_awaitable(expected<Value, Error> o) : o_(o) {}

  bool await_ready() const noexcept {
    return o_.has_value();
  }
  Value await_resume() {
    return o_.value();
  }
  void await_suspend(std::coroutine_handle<promise_type> h) {
    *h.promise().value_ = unexpected<Error>(o_.error());
    h.destroy();
  }
};

template <typename Value, typename Error>
expected_awaitable<Value, Error>
/* implicit */ operator co_await(expected<Value, Error> o) {
  return expected_awaitable<Value, Error>{o};
}

} // namespace folly

struct err {};

folly::expected<int, err> go(folly::expected<int, err> e) {
  co_return co_await e;
}

int main() {
  return go(0).value();
}

// CHECK: define {{.*}} @_Z2goN5folly8expectedIi3errEE

// CHECK: %[[RetVal:.+]] = alloca %"class.folly::expected"
// CHECK: %[[Promise:.+]] = alloca %"struct.folly::expected<int, err>::promise_type"
// CHECK: %[[GroActive:.+]] = alloca i1
// CHECK: %[[CoroGro:.+]] = alloca %"struct.folly::expected<int, err>::promise_type::return_object"

// __promise lifetime should enclose __coro_gro's.

// CHECK: call void @llvm.lifetime.start.p0(i64 8, ptr %[[Promise]])
// CHECK: call void @_ZN5folly8expectedIi3errE12promise_typeC1Ev({{.*}} %[[Promise]])
// CHECK: store i1 false, ptr %[[GroActive]]
// CHECK: call void @llvm.lifetime.start.p0(i64 16, ptr %[[CoroGro]])
// CHECK: call void @_ZN5folly8expectedIi3errE12promise_type17get_return_objectEv({{.*}} %[[CoroGro]],{{.*}} %[[Promise]])
// CHECK: store i1 true, ptr %[[GroActive]]

// Calls into `folly::expected<int, err>::promise_type::return_object::operator folly::expected<int, err>()` to unwrap
// the delayed proxied promise.

// CHECK: call i1 @llvm.coro.end
// CHECK: %[[DelayedConv:.+]] = call i64 @_ZN5folly8expectedIi3errE12promise_type13return_objectcvS2_Ev(ptr noundef nonnull align 8 dereferenceable(16) %[[CoroGro:.+]])
// CHECK: store i64 %[[DelayedConv]], ptr %[[RetVal]]
// CHECK: br label %[[Cleanup0:.+]]

// CHECK: [[Cleanup0]]:
// CHECK:   %[[IsCleanupActive:.+]] = load i1, ptr %[[GroActive]]
// CHECK:   br i1 %[[IsCleanupActive]], label %[[CleanupAction:.+]], label %[[CleanupDone:.+]]

// Call into `folly::expected<int, err>::promise_type::return_object::~return_object()` while __promise is alive.

// CHECK: [[CleanupAction]]:
// CHECK:   call void @_ZN5folly8expectedIi3errE12promise_type13return_objectD1Ev(ptr noundef nonnull align 8 dereferenceable(16) %[[CoroGro]])
// CHECK:   br label %[[CleanupDone]]

// __promise lifetime should end after __coro_gro's.

// CHECK: [[CleanupDone]]:
// CHECK:   call void @llvm.lifetime.end.p0(i64 16, ptr %[[CoroGro]])
// CHECK:   call void @llvm.lifetime.end.p0(i64 8, ptr %[[Promise]])
// CHECK:   call ptr @llvm.coro.free
// CHECK:   br i1 {{.*}}, label %[[CoroFree:.+]], {{.*}}

// Delete coroutine frame.

// CHECK: [[CoroFree]]:
// CHECK: call void @_ZdlPv