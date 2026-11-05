// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o - | FileCheck %s

// A Simon-Tatham switch coroutine: the `switch` has a `case` buried inside a
// scope/loop (Duff's device), so CIRSwitchOpFlattening defers flattening until
// the surrounding loops are gone. While the switch is still structured, the
// separate `for (;;)` loop in the same case is flattened. Its `break` must be
// lowered to the loop's own exit (which runs `status = total;` after the loop),
// NOT to the enclosing switch's exit block. The buggy break-ownership check
// (`getParentOfType<cir::SwitchOp>()`) matched the enclosing deferred switch,
// left the loop's break unlowered, and it was later mis-lowered to the switch
// exit — silently skipping `status = total;` (returning 0 instead of the sum).

int f(int state, unsigned char *pin, unsigned char *pend, int flag) {
  int status = 0;
  int total = 0;
  unsigned char c = 0;
  switch (state) {
  case 0:
    if (flag) {
      do {
        while (pin >= pend) {
          do { status = -1; state = 1; goto common_exit; case 1:; } while (0);
        }
        c = *pin++;
      } while (0);
      total += c;
    }
    for (;;) {
      do {
        while (pin >= pend) {
          do { status = -9; state = 9; goto common_exit; } while (0);
        }
        c = *pin++;
      } while (0);
      if (c == 0)
        break;
      total += c;
    }
    status = total;
  }
common_exit:
  return status;
}

// CHECK-LABEL: cir.func{{.*}} @f
// CHECK: cir.switch.flat

// The infinite `for (;;)` loop head: a constant-true condition whose *false*
// edge is the loop-exit continuation that runs `status = total;`.
// CHECK: cir.const #true
// CHECK-NEXT: cir.brcond %{{.+}} ^{{.+}}, ^[[LOOP_EXIT:bb[0-9]+]]

// The `if (c == 0) break;` test. The break block must branch to the loop exit,
// so that `status = total;` still executes.
// CHECK: cir.cmp(eq
// CHECK-NEXT: cir.brcond %{{.+}} ^[[BREAK:bb[0-9]+]], ^{{.+}}
// CHECK: ^[[BREAK]]:
// CHECK-NEXT: cir.br ^[[LOOP_EXIT]]

// The loop-exit block: reached by both the (dead) condition-false edge and the
// break, it loads `total` and stores it into `status`.
// CHECK: ^[[LOOP_EXIT]]:
// CHECK: cir.load align(4) %[[TOTAL:.+]] : !cir.ptr<!s32i>, !s32i
// CHECK: cir.store align(4) %{{.+}}, %{{.+}} : !s32i, !cir.ptr<!s32i>
