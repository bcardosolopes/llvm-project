// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

namespace std {

template<typename T> struct remove_reference       { typedef T type; };
template<typename T> struct remove_reference<T &>  { typedef T type; };
template<typename T> struct remove_reference<T &&> { typedef T type; };

template<typename T>
typename remove_reference<T>::type &&move(T &&t) noexcept;

struct string {
  string();
};

} // std namespace

// CHECK: ![[StdString:rec_.*]] = !cir.record<struct "std::string" padded {!u8i}>

std::string getstr();
void emplace(std::string &&s);

void t() {
  emplace(std::move(getstr()));
}

// std::move is lowered to a direct pointer operation (matching OGCG).

// CHECK: cir.func {{.*}} @_Z1tv()
// CHECK:   %[[#Addr:]] = cir.alloca ![[StdString]], {{.*}} ["ref.tmp0"]
// CHECK:   cir.call @_Z6getstrv() : () -> ()
// CHECK:   cir.call @_Z7emplaceOSt6string(%[[#Addr]])
// CHECK:   cir.return
// CHECK: }

struct S {
  S() = default;
  S(S&&) = default;

  int val;
};

// Trivial move constructor is replaced with cir.copy, so no function
// definition is emitted for @_ZN1SC1EOS_.

void test_ctor() {
// CHECK-LABEL:   cir.func {{.*}} @_Z9test_ctorv()
// CHECK:           %[[VAR_A:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>
// CHECK:           %[[VAR_B:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>
// CHECK:           cir.copy %[[VAR_A]] to %[[VAR_B]] {{.*}}: !cir.ptr<!rec_S>
// CHECK:           cir.return
// CHECK:         }

  S a;
  S b(std::move(a));
}
