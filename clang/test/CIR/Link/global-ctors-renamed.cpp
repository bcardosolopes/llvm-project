// Constructor-list entries follow their function through the local rename.
//
// Every TU names its dynamic initializers __cxx_global_var_init,
// __cxx_global_var_init.1, ..., all internal, so a later input's are renamed
// in the merge. A cir.global_ctors entry names its function with a string, not
// a symbol reference, and was appended as written: it then named the earlier
// input's function of that name, and the later input's own initializer was
// never called. Here that is JC<C2>::init, which only b.cpp instantiates: its
// registration never ran, while JC<B>'s ran twice.
//
// The shape is a common one in generated bindings: a template static member whose dynamic initializer
// registers the class, instantiated by whichever TUs use it. On
// one real library 45 of 53 classes were never registered.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/a.cpp -o %t/a.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/b.cpp -o %t/b.cir
//
// The precondition: both TUs name their initializers alike.
// RUN: FileCheck --input-file=%t/a.cir %s --check-prefix=NAMES
// RUN: FileCheck --input-file=%t/b.cir %s --check-prefix=NAMES
// NAMES-DAG: cir.func {{.*}}@__cxx_global_var_init()
// NAMES-DAG: cir.func {{.*}}@__cxx_global_var_init.1()
//
// RUN: cir-link %t/a.cir %t/b.cir --emit-llvm --emit-llvm-text -o %t/ab.ll \
// RUN:     --warn-odr-divergence \
// RUN:     2>&1 | FileCheck %s --check-prefix=STATS
// RUN: FileCheck --input-file=%t/ab.ll %s
//
// JC<A>::init is initialized by both TUs, each entry keyed on the member. The
// merge keeps one member, so it keeps one of the two: the first, as the linker
// keeps the comdat it selects and drops the other's entry with it.
// STATS: 1 duplicate initializers dropped
//
// Three entries, one per member initialized; the last is b.cpp's own.
// CHECK: @llvm.global_ctors = appending global [3 x { i32, ptr, ptr }]
// CHECK-SAME: ptr @{{[^,]+}}, ptr @_ZN2JCI1AE4initE }
// CHECK-SAME: ptr @{{[^,]+}}, ptr @_ZN2JCI1BE4initE }
// CHECK-SAME: ptr @[[C2INIT:[^,]+]], ptr @_ZN2JCI2C2E4initE }]
//
// ...and the function it names is the one that registers JC<C2>.
// CHECK: define internal void @[[C2INIT]]()
// CHECK-NOT: define
// CHECK: call void @_ZN3RegC1EPFvvE(ptr {{.*}}@_ZN2JCI2C2E4initE, ptr {{.*}}@_ZN2JCI2C2E5allocEv)

//--- h.h
struct Reg { Reg(void (*f)()); };
template <class C> struct JC {
  static void alloc();
  static const Reg init;
  static void touch() { (void)&init; }
};
template <class C> const Reg JC<C>::init(&JC<C>::alloc);
template <class C> void JC<C>::alloc() {}
struct A {};
struct B {};
struct C2 {};

//--- a.cpp
#include "h.h"
void ua() { JC<A>::touch(); JC<B>::touch(); }

//--- b.cpp
#include "h.h"
void ub() { JC<A>::touch(); JC<C2>::touch(); }
