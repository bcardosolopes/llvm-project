// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR

//
// A label can be touched while building short-circuit CFG and then visited
// again when emitting the LabelStmt itself. This must not attempt to remap
// blockaddress metadata for the same label.

int f(int *p, int n, int a) {
  int m = 0;
  while ((a && p[m]) || (a && ({ if (++m == n) goto done; 1; }))) {
    if (p[m] && ({ if (a) goto done; 1; }))
      --n;
  }
done:
  return n;
}

// CIR-LABEL: cir.func {{.*}} @f
// CIR:       cir.label "done"
// CIR-NOT:   cir.label "done"
