// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir %s

// A `break` wrapped in more than one nested compound statement (scope), inside a
// switch case that is itself nested in a loop. FlattenCFG lowers the break to a
// `cir.br` targeting the switch exit block; every wrapping cir.scope must be
// inlined so that branch stays region-local. Otherwise the greedy driver sees a
// trivially-dead cir.scope (cir.br is Pure, cir.scope has RecursiveMemoryEffects)
// and erases it, which either drops the break branch (miscompile) or — when the
// branch escapes to an outer region — drives RewriterBase::eraseOp's CFG
// post-order block traversal across the region boundary into an unbounded
// recursion (a compiler stack overflow). The -emit-obj RUN line verifies the
// compiler does not crash; -emit-mlir=cir-flat verifies the flattened CFG is
// well-formed.

int cond(void);
void use(int);

int test(int n) {
  for (int i = 0; i < n; ++i) {
    switch (cond()) {
      case 4: {
        if (cond()) {
          { { break; } }
        }
        use(7);
      } break;
      case 5:
        use(5);
        break;
    }
  }
  return 0;
}

// The break must reach the switch exit while `use(7)` stays guarded behind the
// `if`, and the whole function must flatten and return cleanly.
//
// CHECK-LABEL: cir.func{{.*}}@_Z4testi
// CHECK:         cir.switch.flat
// CHECK:         cir.call @_Z3usei
// CHECK:         cir.return

// Only *dead* scopes with a branch escaping the scope may be inlined this way.
// A value-producing cir.scope (here a GNU statement-expression whose result is
// used) must NOT be matched — it only branches within its own region and exits
// via cir.yield, and erasing it would assert (it has uses) / drop the value.
int test_value_scope(int n) {
  int r = 0;
  for (int i = 0; i < n; ++i) {
    switch (cond()) {
      case 1: {
        if (cond()) {
          { { break; } }
        }
        r += ({ int t = cond(); t ? t : 1; });
      } break;
      case 2:
        r += 2;
        break;
    }
  }
  return r;
}

// CHECK-LABEL: cir.func{{.*}}@_Z16test_value_scopei
// CHECK:         cir.switch.flat
// CHECK:         cir.return
