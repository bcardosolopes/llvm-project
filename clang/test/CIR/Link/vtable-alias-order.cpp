// A relative vtable defined through an alias, in an earlier input than a use
// of its address point.
//
// With -fexperimental-relative-c++-abi-vtables and -fno-semantic-interposition
// the vtable is emitted as @_ZTV1S.local and @_ZTV1S is an alias to it. CIRGen
// puts a TU's vtables after the functions that use them, so within one TU the
// address point is always lowered while the alias is still a cir.alias, which
// the lowering follows. A merged module interleaves TUs: here a.cpp defines the
// vtable and b.cpp's constructor uses it, so the alias is lowered first, into
// an llvm.mlir.alias, and the address-point lowering met a form it did not
// accept -- "unexpected symbol type", then "Unable to get value for vtable
// symbol". Linking the same two inputs the other way round worked, which is
// what made it look like a property of the library rather than of the order.
//
// Found on libarmerged_arservicesforwolf.so, whose vtables are all relative.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -fexperimental-relative-c++-abi-vtables -fno-semantic-interposition \
// RUN:     -fPIC -S %t/a.cpp -o %t/a.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -fexperimental-relative-c++-abi-vtables -fno-semantic-interposition \
// RUN:     -fPIC -S %t/b.cpp -o %t/b.cir

// The precondition, so the test cannot pass by the vtable having stopped being
// an alias.
// RUN: FileCheck --input-file=%t/a.cir %s --check-prefix=ALIAS
// ALIAS: cir.alias external @_ZTV1S = @_ZTV1S.local

// The defining input first: the order that failed.
// RUN: cir-link %t/a.cir %t/b.cir --emit-llvm --emit-llvm-text -o %t/ab.ll
// RUN: FileCheck --input-file=%t/ab.ll %s

// And the other order, which always worked, to the same result.
// RUN: cir-link %t/b.cir %t/a.cir --emit-llvm --emit-llvm-text -o %t/ba.ll
// RUN: FileCheck --input-file=%t/ba.ll %s

// The address point is taken through the alias name, as it is in one TU.
// CHECK-DAG: @_ZTV1S = unnamed_addr alias
// CHECK-DAG: define {{.*}}@_Z4makev()
// CHECK-DAG: getelementptr inbounds {{.*}}@_ZTV1S,

//--- a.cpp
struct S {
  virtual ~S();
  virtual int f();
};
S::~S() {}
int S::f() { return 1; }

//--- b.cpp
struct S {
  virtual ~S();
  virtual int f();
};
S *make() { return new S; }
