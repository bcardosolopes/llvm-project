// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm -O1 -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm -O1 -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// CIR now folds switch conditions that are compile-time constants during
// CIRGen, matching OGCG behavior.

// CIR-LABEL: @_Z17test_const_switchv
// CIR-NOT:     cir.switch
// CIR:         #cir.int<1>

// LLVM-LABEL: @_Z17test_const_switchv
// LLVM:         ret i32 1
// LLVM-NEXT:  }

// OGCG-LABEL: @_Z17test_const_switchv
// OGCG:         ret i32 1
// OGCG-NEXT:  }
int test_const_switch() {
  switch (0) {
    case 0: return 1;
    case 1: return 2;
    default: return 3;
  }
}

// CIR-LABEL: @_Z25test_const_switch_defaultv
// CIR-NOT:     cir.switch
// CIR:         #cir.int<3>

// LLVM-LABEL: @_Z25test_const_switch_defaultv
// LLVM:         ret i32 3
// LLVM-NEXT:  }

// OGCG-LABEL: @_Z25test_const_switch_defaultv
// OGCG:         ret i32 3
// OGCG-NEXT:  }
int test_const_switch_default() {
  switch (42) {
    case 0: return 1;
    case 1: return 2;
    default: return 3;
  }
}

// CIR-LABEL: @_Z26test_const_switch_no_matchv
// CIR-NOT:     cir.switch
// CIR:         #cir.int<0>

// LLVM-LABEL: @_Z26test_const_switch_no_matchv
// LLVM:         ret i32 0
// LLVM-NEXT:  }

// OGCG-LABEL: @_Z26test_const_switch_no_matchv
// OGCG:         ret i32 0
// OGCG-NEXT:  }
int test_const_switch_no_match() {
  switch (99) {
    case 0: return 1;
    case 1: return 2;
  }
  return 0;
}
