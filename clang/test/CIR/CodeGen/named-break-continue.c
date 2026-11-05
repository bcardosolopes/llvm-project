// RUN: %clang_cc1 -std=c2y -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -DTEST_NAMED_CONTINUE %s -o - | FileCheck %s --check-prefix=CONTINUE
// RUN: %clang_cc1 -std=c2y -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -DTEST_NAMED_BREAK %s -o - | FileCheck %s --check-prefix=BREAK

#ifdef TEST_NAMED_CONTINUE
int named_continue(int n) {
outer:
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      if (j == 1)
        continue outer;
    }
  }
  return 0;
}

// CONTINUE-LABEL: cir.func {{.*}} @named_continue
// CONTINUE: cir.label "outer"
// CONTINUE: cir.if
// CONTINUE: cir.goto "named_continue.__named_continue.{{[0-9]+}}"
// CONTINUE: } step {
// CONTINUE: cir.label "named_continue.__named_continue.{{[0-9]+}}"
#endif

#ifdef TEST_NAMED_BREAK
int named_break(int n) {
outer:
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      if (j == 2)
        break outer;
    }
  }
  return 0;
}

// BREAK-LABEL: cir.func {{.*}} @named_break
// BREAK: cir.label "outer"
// BREAK: cir.if
// BREAK: cir.goto "named_break.__named_break.{{[0-9]+}}"
// BREAK: cir.label "named_break.__named_break.{{[0-9]+}}"
// BREAK: {{.*}}cir.return
#endif
