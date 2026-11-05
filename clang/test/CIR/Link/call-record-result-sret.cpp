// A class returned across TUs whose records differ only in name is built in
// the caller's return slot, not in a temporary copied bitwise into it.
//
// The two TUs below disagree on Inner's body (an ODR violation of the kind two
// libc++ configurations produce for std::set), so the merge keeps two records
// for T as well, and the call from wrap() to make() disagrees on the result
// type. Bridged by a bitcast of the value, it lowered to a call into a
// temporary and a load/store of the whole T into wrap()'s slot: the object
// make() built, with its self pointer, was not the one wrap()'s caller got.
// An app crashed at startup on exactly that (a record holding a std::set).
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -O2 -fclangir \
// RUN:     -fclangir-fat-objects -c %t/caller.cpp -o %t/caller.o
// RUN: %clang --target=aarch64-unknown-linux-android -O2 -fclangir \
// RUN:     -fclangir-fat-objects -c %t/callee.cpp -o %t/callee.o
// RUN: cir-link %t/caller.o %t/callee.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/out.ll
// RUN: FileCheck --input-file=%t/out.ll %s
//
// CHECK:       define {{.*}}@_Z4wrapv(ptr {{.*}}sret({{.*}}) {{.*}}%[[SLOT:[0-9]+]])
// CHECK-NOT:   load %struct.T
// CHECK:       call void @_Z4makev(ptr {{.*}}sret({{.*}}) {{.*}}%[[SLOT]])
// CHECK-NOT:   store %struct.T
// CHECK:       ret void

//--- caller.cpp
struct Inner {
  int a;
  float b;
};
struct T {
  T *self;
  Inner inner;
  long pad[4];
  T();
  T(const T &);
  ~T();
};
T make();
T wrap() { return make(); }

//--- callee.cpp
struct Inner {
  float a;
  int b;
};
struct T {
  T *self;
  Inner inner;
  long pad[4];
  T() : self(this) {}
  T(const T &) : self(this) {}
  ~T();
};
T make() { return T(); }
