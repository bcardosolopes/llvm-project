// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir %s

// A nested compound-statement block inside a switch case must open its own
// cleanup scope, so its local's destructor runs at the block's closing brace
// -- not deferred to the end of the case/switch. This is the constant-folded
// switch path (switch (1) folds away the cir.switch), which previously emitted
// the nested block via emitCompoundStmtWithoutScope and dropped the scope.

struct T {
  int id;
  T(int i);
  ~T();
};

// CHECK-LABEL: @_Z1fv
// CHECK-NOT:     cir.switch
//
// First nested block: ctor and dtor for `l` are paired inside one cir.scope.
// CHECK:         cir.scope {
// CHECK:           %[[L:.*]] = cir.alloca !rec_T, {{.*}} ["l", init]
// CHECK:           cir.call @_ZN1TC1Ei(%[[L]]
// CHECK:           cir.call @_ZN1TD1Ev(%[[L]]
// CHECK:         }
//
// Second nested block: ctor and dtor for `m` are paired inside its own scope.
// CHECK:         cir.scope {
// CHECK:           %[[M:.*]] = cir.alloca !rec_T, {{.*}} ["m", init]
// CHECK:           cir.call @_ZN1TC1Ei(%[[M]]
// CHECK:           cir.call @_ZN1TD1Ev(%[[M]]
// CHECK:         }
void f() {
  switch (1) {
  case 1:
    { T l(1); }
    { T m(2); }
    break;
  }
}
