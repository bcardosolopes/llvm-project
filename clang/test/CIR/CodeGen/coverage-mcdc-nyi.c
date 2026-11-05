// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fprofile-instrument=clang -fcoverage-mapping -fcoverage-mcdc \
// RUN:   -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s

// MC/DC coverage instrumentation (test-vector bitmaps and per-condition
// tracking) is not yet implemented in ClangIR. The shared coverage-mapping
// generator still emits MC/DC decision regions, so emitting a binary without
// the backing instrumentation makes llvm-cov silently report 0% MC/DC coverage
// regardless of the tests run. Reject the mode with a clear diagnostic instead.

// CHECK: ClangIR code gen Not Yet Implemented: MC/DC code coverage (-fcoverage-mcdc)

int decide(int a, int b) {
  if (a && b)
    return 1;
  return 0;
}
