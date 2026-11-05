// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm %s -o - | FileCheck %s

// Regression: when a system-header explicit instantiation calls an
// inline hidden+abi_tagged fpos constructor, ClangIR must emit the constructor
// definition in this TU (not just a declaration), otherwise final link can fail
// with undefined std::__1::fpos<mbstate_t>::fpos[abi:fn190106](long long).
# 1 "sys.hpp" 1 3
namespace std {
inline namespace __1 {
struct mbstate_t {};

template <class StateT>
class fpos {
  StateT st;
  long long off;

public:
  __attribute__((visibility("hidden"), exclude_from_explicit_instantiation,
                 abi_tag("fn190106")))
  fpos(long long o = 0) : st(), off(o) {}
};

template <class CharT>
struct char_traits {};

template <class CharT, class Traits>
struct basic_streambuf {
  using pos_type = fpos<mbstate_t>;
  using off_type = long long;
  virtual pos_type seekoff(off_type, int, unsigned = 0) {
    return pos_type(off_type(-1));
  }
};
} // namespace __1
} // namespace std
# 2 "test.cpp" 2

template class std::__1::basic_streambuf<char, std::__1::char_traits<char>>;

// CHECK: define weak_odr {{.*}} @_ZNSt3__115basic_streambufIcNS_11char_traitsIcEEE7seekoffExij
// CHECK: call void @_ZNSt3__14fposINS_9mbstate_tEEC1B8fn190106Ex
// CHECK: define linkonce_odr hidden void @_ZNSt3__14fposINS_9mbstate_tEEC2B8fn190106Ex
// CHECK-NOT: declare hidden void @_ZNSt3__14fposINS_9mbstate_tEEC2B8fn190106Ex
