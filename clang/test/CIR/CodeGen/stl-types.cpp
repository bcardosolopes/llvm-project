// RUN: %clang -fclangir -std=c++17 -fcxx-exceptions -fexceptions -c %s -o %t.o
//
// Test that common STL types compile correctly with CIR.
// These patterns require the fix (on-demand template instantiation)
// and fix (devirtualized destructor calls).

#include <utility>
#include <memory>
#include <functional>
#include <optional>

// std::forward / std::move
template<typename T>
T&& my_wrapper(T&& x) { return std::forward<T>(x); }

struct Widget {
  int x;
  Widget(int x) : x(x) {}
  Widget(Widget&& o) : x(o.x) { o.x = 0; }
  ~Widget() {}
};

Widget test_forward() {
  int v = 42;
  return Widget(my_wrapper<int>(std::move(v)));
}

// std::pair
std::pair<int, double> test_pair() {
  return std::make_pair(1, 2.0);
}

// std::unique_ptr
int test_unique_ptr() {
  auto p = std::make_unique<Widget>(42);
  return p->x;
}

// std::shared_ptr
int test_shared_ptr() {
  auto p = std::make_shared<Widget>(42);
  auto q = p;
  return p.use_count();
}

// std::optional
int test_optional() {
  std::optional<int> o = 42;
  return o.value_or(0);
}

// std::function
int test_function() {
  std::function<int(int)> fn = [](int x) { return x * 2; };
  return fn(21);
}

// std::variant
#include <variant>
int test_variant() {
  std::variant<int, double> v = 42;
  return std::get<int>(v);
}

// std::any
#include <any>
int test_any() {
  std::any a = 42;
  return std::any_cast<int>(a);
}

// std::exception_ptr
#include <exception>
#include <stdexcept>
std::exception_ptr test_exception_ptr() {
  try { throw std::runtime_error("test"); }
  catch (...) { return std::current_exception(); }
  return nullptr;
}

// std::vector with emplace_back + range-for
#include <algorithm>
int test_emplace_sort() {
  std::vector<int> v;
  v.emplace_back(3);
  v.emplace_back(1);
  v.emplace_back(2);
  std::sort(v.begin(), v.end());
  return v[0]; // 1
}

// Inline member functions with std::string
#include <cstdio>
struct Logger {
  std::string prefix;
  Logger(const char* p) : prefix(p) {}
  ~Logger() {}
  void log(const char* msg) { printf("[%s] %s\n", prefix.c_str(), msg); }
};
void test_inline_member() {
  Logger l("test");
  l.log("hello");
}
