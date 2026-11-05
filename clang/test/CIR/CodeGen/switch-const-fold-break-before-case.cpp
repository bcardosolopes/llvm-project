// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm -O1 -o - %s | FileCheck %s --check-prefix=LLVM

void use(int);

// Constant-folded switch with dead `break; case` fragments should still fold
// to straight-line code and avoid materializing a cir.switch.
void folded_break_before_case() {
  switch (1) {
    break;
  case 1:
    use(1);
    break;
    break;
  case 2:
    use(2);
    break;
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z24folded_break_before_casev
// CIR-NOT: cir.switch
// CIR: %[[ONE:.*]] = cir.const #cir.int<1>
// CIR: cir.call @_Z3usei(%[[ONE]])

// LLVM-LABEL: define{{.*}} @_Z24folded_break_before_casev
// LLVM-NOT: switch
// LLVM: call void @_Z3usei(i32 noundef 1)

void folded_label_before_case() {
  switch (1) {
  L:
  case 1:
    use(1);
    break;
  case 2:
    use(2);
    break;
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z24folded_label_before_casev
// CIR: cir.switch
// CIR: %[[ONE2:.*]] = cir.const #cir.int<1>
// CIR: cir.call @_Z3usei(%[[ONE2]])

// LLVM-LABEL: define{{.*}} @_Z24folded_label_before_casev
// LLVM-NOT: switch
// LLVM: call void @_Z3usei(i32 noundef 1)
