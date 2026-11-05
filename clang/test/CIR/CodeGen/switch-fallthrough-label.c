// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Regression test: switch case fallthrough to a goto label + case combination.
// When a case falls through to a label that precedes another case, the
// fallthrough must reach the label/case, not skip over it to a later case
// that happens to appear earlier in the CIR switch body.

int test(int action) {
    int result = 0;
    switch (action) {
    case 3:
        result = 30;
        // fallthrough
    do_vivify:
    case 4:
        result += 100;
        break;
    case 12:
        result = 120;
        break;
    }
    return result;
}

// Verify that case 3 yields (falls through), case 4 is in the label block
// after case 3, and case 12 is after case 4. This ensures case 3's
// fallthrough reaches case 4 through the label, not case 12.

// CHECK-LABEL: cir.func {{.*}}@test
// CHECK:       cir.switch
// Case 3 with fallthrough (yield, not break)
// CHECK:       cir.case(equal, [#cir.int<3>
// CHECK:       #cir.int<30>
// CHECK:       cir.yield
// The label block must come next, with case 4 and case 12 AFTER it
// CHECK:       cir.label "do_vivify"
// CHECK:       cir.case(equal, [#cir.int<4>
// CHECK:       cir.binop(add,
// CHECK:       cir.break
// CHECK:       cir.case(equal, [#cir.int<12>
// CHECK:       #cir.int<120>
// CHECK:       cir.break
