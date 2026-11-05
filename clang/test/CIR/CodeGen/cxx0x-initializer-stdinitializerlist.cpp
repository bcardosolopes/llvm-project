// RUN: %clang_cc1 -std=c++11 -triple x86_64-none-linux-gnu -fmerge-all-constants -fclangir -emit-llvm -o - %s | FileCheck -check-prefixes=CIR %s
// RUN: %clang_cc1 -std=c++11 -triple x86_64-none-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm -o - %s | FileCheck -check-prefixes=CIR-EH %s

// CIR codegen for std::initializer_list temporaries: destructor sequencing on
// the normal path (CIR) and the shared exception-cleanup landing pad emitted by
// cir.cleanup.scope on the exceptions-enabled path (CIR-EH). Migrated out of
// clang/test/CodeGenCXX/cxx0x-initializer-stdinitializerlist.cpp: CIR coverage
// must live under clang/test/CIR/, never bolted onto classical codegen tests.

namespace std {
  typedef decltype(sizeof(int)) size_t;

  // libc++'s implementation
  template <class _E>
  class initializer_list
  {
    const _E* __begin_;
    size_t    __size_;

    initializer_list(const _E* __b, size_t __s)
      : __begin_(__b),
        __size_(__s)
    {}

  public:
    typedef _E        value_type;
    typedef const _E& reference;
    typedef const _E& const_reference;
    typedef size_t    size_type;

    typedef const _E* iterator;
    typedef const _E* const_iterator;

    initializer_list() : __begin_(nullptr), __size_(0) {}

    size_t    size()  const {return __size_;}
    const _E* begin() const {return __begin_;}
    const _E* end()   const {return __begin_ + __size_;}
  };
}

struct destroyme1 {
  ~destroyme1();
};
struct destroyme2 {
  ~destroyme2();
};
struct witharg1 {
  witharg1(const destroyme1&);
  ~witharg1();
};

void fn3() {
  // objects should be destroyed after dm2
  // CIR-LABEL: define{{.*}} void @_Z3fn3v
  auto list = { destroyme1(), destroyme1() };
  destroyme2 dm2;
  // CIR: call void @_ZN10destroyme2D1Ev
  // CIR: call void @_ZN10destroyme1D1Ev
}

void fn5() {
  // temps should be destroyed before dm2
  // objects should be destroyed after dm2
  // CIR-LABEL: define{{.*}} void @_Z3fn5v
  auto list = { witharg1(destroyme1()), witharg1(destroyme1()) };
  destroyme2 dm2;
  // CIR: call void @_ZN10destroyme1D1Ev
  // CIR: call void @_ZN10destroyme1D1Ev
  // CIR: call void @_ZN10destroyme2D1Ev
  // CIR: call void @_ZN8witharg1D1Ev
}

void fn8() {
  // CIR-LABEL: define{{.*}} void @_Z3fn8v
  // CIR-EH-LABEL: define{{.*}} void @_Z3fn8v()
  // CIR-EH-SAME: personality ptr @__gxx_personality_v0
  void target(std::initializer_list<std::initializer_list<destroyme1>>);
  // objects should be destroyed before dm2, after call returns
  // CIR-NOT: call void @_ZN10destroyme1D1Ev
  // CIR: call void @_Z6targetSt16initializer_listIS_I10destroyme1EE
  std::initializer_list<destroyme1> inner;
  target({ inner, { destroyme1() } });
  destroyme2 dm2;
  // CIR: call void @_ZN10destroyme1D1Ev
  // CIR: call void @_ZN10destroyme2D1Ev
  // CIR-EH-NOT: call void @_ZN10destroyme1D1Ev
  // CIR-EH: invoke void @_Z6targetSt16initializer_listIS_I10destroyme1EE
  // CIR-EH-NEXT: to label %[[FN8_CONT:[0-9]+]] unwind label %[[FN8_LPAD:[0-9]+]]
  // CIR-EH: [[FN8_CONT]]:
  // CIR-EH: [[FN8_LPAD]]:
  // CIR-EH: landingpad { ptr, i32 }
  // CIR-EH: call void @_ZN10destroyme1D1Ev
  // CIR-EH: [[FN8_RESUME:[0-9]+]]:
  // CIR-EH: resume { ptr, i32 }
  // CIR-EH: call void @_ZN10destroyme1D1Ev
  // CIR-EH: call void @_ZN10destroyme2D1Ev
}

void fn9() {
  // objects should be destroyed after dm2
  // CIR-LABEL: define{{.*}} void @_Z3fn9v
  std::initializer_list<destroyme1> inner;
  std::initializer_list<std::initializer_list<destroyme1>> list =
      { inner, { destroyme1() } };
  destroyme2 dm2;
  // CIR: call void @_ZN10destroyme2D1Ev
  // CIR: call void @_ZN10destroyme1D1Ev
}

namespace dtors {
  struct S {
    S();
    ~S();
  };
  void z();

  // CIR-LABEL: define{{.*}} void @_ZN5dtors1hEv(
  void h() {
    // CIR: call void @_ZN5dtors1SC1Ev(
    // CIR: call void @_ZN5dtors1SC1Ev(
    std::initializer_list<S> x = { S(), S() };

    // CIR-NOT: call void @_ZN5dtors1SD1Ev(

    // CIR: call void @_ZN5dtors1zEv(
    z();

    // Destruction loop for underlying array.
    // CIR: call void @_ZN5dtors1SD1Ev(
  }
}
