// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm -O1 -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm -O1 -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// CIR matches OGCG by eliding dead arms of constant-folded if-statements when
// the skipped arm has no labels.

void side_effect_a();
void side_effect_b();

// CIR-LABEL: @_Z13test_const_trv
// CIR:         cir.call @_Z13side_effect_av()
// CIR-NOT:     @_Z13side_effect_bv

// LLVM-LABEL: @_Z13test_const_trv
// LLVM:         call void @_Z13side_effect_av()
// LLVM-NOT:     @_Z13side_effect_bv
// LLVM:         ret void

// OGCG-LABEL: @_Z13test_const_trv
// OGCG:         call void @_Z13side_effect_av()
// OGCG-NOT:     @_Z13side_effect_bv
// OGCG:         ret void
void test_const_tr() {
  if (1)
    side_effect_a();
  else
    side_effect_b();
}

// CIR-LABEL: @_Z16test_const_falsev
// CIR:         cir.call @_Z13side_effect_bv()
// CIR-NOT:     @_Z13side_effect_av

// LLVM-LABEL: @_Z16test_const_falsev
// LLVM:         call void @_Z13side_effect_bv()
// LLVM-NOT:     @_Z13side_effect_av
// LLVM:         ret void

// OGCG-LABEL: @_Z16test_const_falsev
// OGCG:         call void @_Z13side_effect_bv()
// OGCG-NOT:     @_Z13side_effect_av
// OGCG:         ret void
void test_const_false() {
  if (0)
    side_effect_a();
  else
    side_effect_b();
}

// CIR-LABEL: @_Z18test_const_no_elsev
// CIR-NOT:     cir.if
// CIR-NOT:     @_Z13side_effect_av
// CIR:         cir.return

// LLVM-LABEL: @_Z18test_const_no_elsev
// LLVM:         ret void
// LLVM-NEXT:  }

// OGCG-LABEL: @_Z18test_const_no_elsev
// OGCG:         ret void
// OGCG-NEXT:  }
void test_const_no_else() {
  if (0)
    side_effect_a();
}
