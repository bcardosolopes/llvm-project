// RUN: %clang_cc1 -std=gnu++20 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu %s -o /dev/null

// Pointer-to-member of a non-first union field previously crashed
// because CIR's DataMemberAttr verifier requires the member type at the given
// index to match the attribute's member type. Fixed by using the byte-offset
// path for union member pointers (all union members are at offset 0).

union U {
  bool b;
  int i;
};

int U::* test() {
  return &U::i;
}
