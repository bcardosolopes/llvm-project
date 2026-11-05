// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir \
// RUN:   -fno-clangir-call-conv-lowering -emit-llvm -std=c++20 -w %s -o %t.ll
// RUN: FileCheck %s -input-file=%t.ll

// goto+case inside switch with nested switch and enum type.
// Previously crashed with "cir.cast op destroyed but still has uses"
// or sentinel assertion during switch flattening.

enum E : unsigned char { A };

void f() {
  E s{};
  char e = 0;
  switch (s) {
      { goto L; case A: ; }
    L:
      switch (e) {
        default: return;
      }
  }
}

// CHECK: define {{.*}} @_Z1fv
