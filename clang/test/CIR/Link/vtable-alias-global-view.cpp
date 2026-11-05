// A constant-initialized object pointing into a relative vtable that a later
// input defines through an alias.
//
// With -fexperimental-relative-c++-abi-vtables and -fno-semantic-interposition
// the vtable is emitted as @_ZTV1S.local and @_ZTV1S is an alias to it. An
// object with a constexpr constructor is initialized in place, its vptr a
// #cir.global_view of the vtable. In a merged module that global comes from
// a.cpp, ahead of b.cpp's alias, and is lowered while the alias is still a
// cir.alias -- which the global_view lowering only accepted once lowered, as
// an llvm.mlir.alias: "#cir.global_view symbol @_ZTV1S is not a supported
// global/function/alias symbol". The other order always worked.
//
// Found in an audio library.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -fexperimental-relative-c++-abi-vtables -fno-semantic-interposition \
// RUN:     -fPIC -S %t/a.cpp -o %t/a.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -fexperimental-relative-c++-abi-vtables -fno-semantic-interposition \
// RUN:     -fPIC -S %t/b.cpp -o %t/b.cir
//
// The preconditions: a.cpp's object is initialized with a view of the vtable,
// and b.cpp defines the vtable through the alias.
// RUN: FileCheck --input-file=%t/a.cir %s --check-prefix=VIEW
// VIEW: cir.global external @g = #cir.const_record<{#cir.global_view<@_ZTV1S,
// RUN: FileCheck --input-file=%t/b.cir %s --check-prefix=ALIAS
// ALIAS: cir.alias external @_ZTV1S = @_ZTV1S.local
//
// The object's input first: the order that failed.
// RUN: cir-link %t/a.cir %t/b.cir --emit-llvm --emit-llvm-text -o %t/ab.ll
// RUN: FileCheck --input-file=%t/ab.ll %s
//
// And the other order, which always worked, to the same result.
// RUN: cir-link %t/b.cir %t/a.cir --emit-llvm --emit-llvm-text -o %t/ba.ll
// RUN: FileCheck --input-file=%t/ba.ll %s
//
// The vptr is the address point, taken through the alias name.
// CHECK-DAG: @_ZTV1S = unnamed_addr alias
// CHECK-DAG: @g = global { ptr } { ptr getelementptr inbounds inrange(-16, 24) ({ [5 x i32] }, ptr @_ZTV1S, i32 0, i32 0, i32 2) }

//--- a.cpp
struct S {
  constexpr S() {}
  virtual ~S();
  virtual int f();
};
S g;

//--- b.cpp
struct S {
  constexpr S() {}
  virtual ~S();
  virtual int f();
};
S::~S() {}
int S::f() { return 1; }
