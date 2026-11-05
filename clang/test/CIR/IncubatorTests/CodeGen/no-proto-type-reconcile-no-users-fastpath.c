// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Compile-time regression coverage for declaration-only no-proto type
// reconciliation: when a no-proto declaration is later refined to a typed
// declaration before any symbol users are created, reconciliation should still
// produce the correct typed signature.

int noUsersFastPath();
struct U;
int noUsersFastPath(struct U *);

struct U {
  int x;
};

int noUsersFastPath(struct U *p) { return p->x; }

// CHECK-LABEL: cir.func{{.*}} @noUsersFastPath(%arg0: !cir.ptr<!rec_U>
// CHECK-NOT: no_proto
