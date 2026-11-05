// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify that CIR does not emit spurious !invariant.group metadata on
// parameter allocas or reference-type allocas, matching OGCG behavior.
// OGCG only uses !invariant.group for vtable pointer stores/loads under
// -fstrict-vtable-pointers.

struct B {
  int &ref;
  B(int &r) : ref(r) {}
  int &get();
};

// Reference parameter — no !invariant.group.
void test_ref_param(int &r) {
  int x = r;
}
// CHECK-LABEL: @_Z14test_ref_paramRi
// CHECK-NOT: invariant.group
// CHECK: }
// OGCG-LABEL: @_Z14test_ref_paramRi
// OGCG-NOT: invariant.group
// OGCG: }

// Const parameter — no !invariant.group.
void test_const_param(const int x) {
  int y = x;
}
// CHECK-LABEL: @_Z16test_const_parami
// CHECK-NOT: invariant.group
// CHECK: }
// OGCG-LABEL: @_Z16test_const_parami
// OGCG-NOT: invariant.group
// OGCG: }

// Const reference parameter — no !invariant.group.
void test_const_ref_param(const int &r) {
  int x = r;
}
// CHECK-LABEL: @_Z20test_const_ref_paramRKi
// CHECK-NOT: invariant.group
// CHECK: }
// OGCG-LABEL: @_Z20test_const_ref_paramRKi
// OGCG-NOT: invariant.group
// OGCG: }

// Reference member access — no !invariant.group.
void test_ref_member(B &b) {
  int x = b.ref;
}
// CHECK-LABEL: @_Z15test_ref_memberR1B
// CHECK-NOT: invariant.group
// CHECK: }
// OGCG-LABEL: @_Z15test_ref_memberR1B
// OGCG-NOT: invariant.group
// OGCG: }

// Const local — no !invariant.group on entry-block allocas either,
// matching OGCG behavior.
int produce_int();
void use_ref(const int &);

int test_const_local() {
  const int x = produce_int();
  use_ref(x);
  return x;
}
// CHECK-LABEL: @_Z16test_const_localv
// CHECK-NOT: invariant.group
// CHECK: }
// OGCG-LABEL: @_Z16test_const_localv
// OGCG-NOT: invariant.group
// OGCG: }
