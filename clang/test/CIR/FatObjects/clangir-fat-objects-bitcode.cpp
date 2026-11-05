// RUN: %clang -target aarch64-none-linux-android21 -fclangir \
// RUN:   -fclangir-fat-objects -fclangir-inline -O2 -c %s -o %t.o
// RUN: llvm-readelf -S %t.o | FileCheck %s --check-prefix=SECTION
// RUN: llvm-objcopy --dump-section=.llvm.cir.bitcode=%t.bc %t.o /dev/null
// RUN: llvm-dis %t.bc -o - | FileCheck %s --check-prefix=BITCODE
// RUN: %clang -target aarch64-none-linux-android21 -fclangir -O2 -c %s \
// RUN:   -o %t.plain.o
// RUN: llvm-readelf -S %t.plain.o | FileCheck %s --check-prefix=PLAIN

// A ClangIR fat object also carries the bitcode of its CIR, for the link steps
// that read bitcode rather than CIR: a cross-library optimizer's export
// analysis reads what a relink of the CIR may
// reference, as it does in a -ffat-lto-objects object's .llvm.lto.
//
//   1. the section is SHF_EXCLUDE, so the shipped library stays clean, and it
//      is not .llvm.lto, which LLD's --fat-lto-objects would link in place of
//      the native code;
//   2. the bitcode is the CIR as a relink lowers it, before the CIR inliner.
//      The object's own lowering inlines W<S>::put, an extern template member
//      that another library defines, and drops its available_externally body;
//      a relink that does not inline it calls it, so the bitcode has to show
//      that entry calls it.

// SECTION: .llvm.cir.bitcode
// SECTION-SAME: E{{ }}
// SECTION-NOT: .llvm.lto

// BITCODE-DAG: define available_externally void @_ZN1WI1SE3putEc(
// BITCODE-DAG: call void @_ZN1WI1SE3putEc(

// PLAIN-NOT: .llvm.cir.bitcode

void provider(char);
struct S {
  void set(char c) { provider(c); }
};
template <class T> struct W {
  T t;
  void put(char c) { t.set(c); }
};
extern template struct W<S>;
void entry(W<S> &w) { w.put('x'); }
