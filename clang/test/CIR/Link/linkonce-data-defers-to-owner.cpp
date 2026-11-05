// A linkonce copy of a global does not displace the external definition other
// inputs declare.
//
// Built with -fno-rtti, a TU that throws a class emits the class's typeinfo as
// a linkonce copy: it cannot assume the TU holding the key function emitted
// RTTI. A TU built with RTTI declares the same typeinfo external and relies on
// that owner. Merged, the copy prevailed as the only definition, and the
// library carried its own typeinfo for the class -- a different type, to any
// catch clause in another library, than the one thrown there. One real
// library did this to std::bad_function_call, whose owner is libc++.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S -O1 %t/rtti.cpp -o %t/rtti.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S -O1 -fno-rtti %t/nortti.cpp -o %t/nortti.cir
//
// The precondition: one declares the typeinfo, the other defines a copy.
// RUN: FileCheck --input-file=%t/rtti.cir %s --check-prefix=DECL
// DECL: cir.global {{.*}}external @_ZTI1E :
// RUN: FileCheck --input-file=%t/nortti.cir %s --check-prefix=COPY
// COPY: cir.global {{.*}}linkonce_odr {{.*}}@_ZTI1E =
//
// The merges run on fat objects: deferral keys on each TU's RTTI flag, which
// only fat objects carry.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -c %t/rtti.cpp -o %t/rtti.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -fno-rtti -c %t/nortti.cpp -o %t/nortti.o
//
// Both orders -- the external declaration first and the linkonce definition
// first: the typeinfo stays external, and both throws use it.
// RUN: cir-link %t/rtti.o %t/nortti.o --warn-odr-divergence --emit-llvm \
// RUN:     --emit-llvm-text -o %t/a.ll 2>&1 | FileCheck %s --check-prefix=STATS
// RUN: cir-link %t/nortti.o %t/rtti.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/b.ll
// RUN: FileCheck --input-file=%t/a.ll %s
// RUN: FileCheck --input-file=%t/b.ll %s
// STATS: warning: linkonce definition of '_ZTI1E' dropped in favour of the external definition other inputs declare
// STATS: 1 globals deferred to their owners
// CHECK: @_ZTI1E = external constant
// CHECK-DAG: call void @__cxa_throw(ptr {{.*}}, ptr @_ZTI1E,
// CHECK-DAG: call void @__cxa_throw(ptr {{.*}}, ptr @_ZTI1E,
//
// When the link says which symbols its shared libraries define, the copy is
// deferred only if one of them is the owner -- libc++_shared.so defining
// std::bad_function_call's typeinfo -- and otherwise stays: a library's
// copies of a vendored libc++'s typeinfo have no owner it links against.
// RUN: echo _ZTI1E > %t/owner.txt
// RUN: cir-link %t/rtti.o %t/nortti.o --shared-library-symbols=%t/owner.txt \
// RUN:     --emit-llvm --emit-llvm-text -o %t/owned.ll
// RUN: FileCheck --input-file=%t/owned.ll %s
// RUN: echo _ZTI1F > %t/other.txt
// RUN: cir-link %t/rtti.o %t/nortti.o --shared-library-symbols=%t/other.txt \
// RUN:     --emit-llvm --emit-llvm-text -o %t/unowned.ll
// RUN: FileCheck --input-file=%t/unowned.ll %s --check-prefix=ALONE
//
// Without a TU declaring it external, the copy is all there is, and stays.
// RUN: cir-link %t/nortti.o --emit-llvm --emit-llvm-text -o %t/alone.ll
// RUN: FileCheck --input-file=%t/alone.ll %s --check-prefix=ALONE
// ALONE: @_ZTI1E = linkonce_odr {{.*}}constant

//--- rtti.cpp
struct E {
  virtual ~E(); // key function, defined in another library
};
void throw_with_rtti() { throw E(); }

//--- nortti.cpp
struct E {
  virtual ~E();
};
void throw_without_rtti() { throw E(); }
