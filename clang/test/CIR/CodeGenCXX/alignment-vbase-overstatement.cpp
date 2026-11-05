// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// CIR must use non-virtual alignment for references/pointers to
// non-final C++ classes with virtual bases. A virtual base's aligned member
// does not change the derived class's ABI alignment.

namespace test1 {
  struct A {
    __attribute__((aligned(16))) int aArray[4];
  };
  struct B : virtual A {
    int x;
    void *y;
    int bArray[4];
    unsigned bitfield : 3;
  };

  // B's ABI alignment is 8 (vtable pointer), not 16.
  // Check both function signature and !align metadata on reference load.
  // LLVM-LABEL: define {{.*}} @_ZN5test11eERNS_1BE
  // LLVM-SAME: align 8
  // LLVM: load ptr, {{.*}}!align [[ALIGN8:![0-9]+]]
  // OGCG-LABEL: define {{.*}} @_ZN5test11eERNS_1BE
  // OGCG-SAME: align 8
  // OGCG: load ptr, {{.*}}!align [[ALIGN8:![0-9]+]]
  unsigned e(B &bref) {
    return bref.bitfield;
  }

  // Final class can use full alignment.
  struct C final : virtual A {
    int x;
  };

  // LLVM-LABEL: define {{.*}} @_ZN5test11cERNS_1CE
  // LLVM-SAME: align 16
  // OGCG-LABEL: define {{.*}} @_ZN5test11cERNS_1CE
  // OGCG-SAME: align 16
  void c(C &cref) {
    (void)cref;
  }
}

// OGCG: [[ALIGN8]] = !{i64 8}
