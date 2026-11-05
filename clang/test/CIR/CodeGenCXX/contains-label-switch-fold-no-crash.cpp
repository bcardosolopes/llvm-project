// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -std=gnu++20 -fclangir -fclangir-mem2reg -emit-obj -o /dev/null %s

// This exercises the constant-folded switch statement collection path that
// repeatedly queries CIRGenFunction::containsLabel from nested if/switch
// statement emission.

static int fold_switch_with_nested_if(int x) {
  if (x > 4) {
    if ((x ?: 1) > 0) {
      switch (0) {
      case 0:
        if ((x ?: 2) > 1)
          return x;
        break;
      default:
        return -1;
      }
    }
  }
  return 0;
}

int t2459_entry(int x) { return fold_switch_with_nested_if(x); }
