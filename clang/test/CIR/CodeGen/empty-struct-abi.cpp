// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s < %t.ll

// Test that empty structs are not passed as arguments per x86_64 ABI.
// The struct has a user-declared default ctor but an implicit trivial copy
// ctor and no data members, so it should be classified as empty (Ignore)
// and not passed as an argument.
//
// Fixed by the numRealMembers padding skip.
//

struct S {
  S();
};

void Call(S) {}

// Empty struct should not be passed as an argument
// CHECK: define {{.*}}void @_Z4Call1S()
