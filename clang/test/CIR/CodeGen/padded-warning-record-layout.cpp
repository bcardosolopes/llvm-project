// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-llvm -o - -Wpadded -Werror %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-llvm -o - -Wpadded -Werror %s | FileCheck %s --check-prefix=OGCG

struct __attribute__((packed)) Packed {
  char c;
  int i;
};

Packed make_packed(void) {
  return Packed{0, 42};
}

// LLVM-LABEL: define{{.*}} @_Z11make_packedv
// OGCG-LABEL: define{{.*}} @_Z11make_packedv
