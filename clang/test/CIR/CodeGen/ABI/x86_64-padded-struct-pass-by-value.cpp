// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that padded structs with alignment padding between fields are
// correctly passed by value using two registers on x86_64.
// This catches a CIR ABI lowering bug where the classify() loop skipped
// the second field of a padded record because `numRealMembers` didn't
// account for interstitial padding members.

struct alignas(8) Opaque { char x; };

// This struct has explicit padding between ToState and Ptr:
//   { i32 ToState, [4 x i8] padding, Opaque* Ptr }
// Total: 16 bytes, passed in two integer registers on x86_64.
struct Entry {
  int ToState;
  Opaque *Ptr;
};

// CHECK-LABEL: define {{.*}} @_Z4testii
// The struct must be passed as two arguments (i32, ptr), not just i32.
// CHECK: call void @_Z7receive5Entry(i32 %{{.*}}, ptr %{{.*}})
void receive(Entry e);
void test(int a, int b) {
  Entry e;
  e.ToState = a;
  e.Ptr = nullptr;
  receive(e);
}

// CHECK: declare void @_Z7receive5Entry(i32, ptr)
