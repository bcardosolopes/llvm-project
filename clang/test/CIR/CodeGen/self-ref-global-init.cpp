// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Test self-referential global initializers: a struct with a pointer member
// that points to another member within the same struct. The constant
// initializer must use a self-referencing getelementptr.

namespace RefTempSubobject {
  struct SelfReferential {
    int *p = ints;
    int ints[3] = {1, 2, 3};
  };

  // The global temporary for 'sr' has a self-referential initializer:
  // the 'p' member points to the 'ints' member within the same global.
  constexpr const SelfReferential &sr = SelfReferential();
}

// CIR: cir.global "private"  internal  @_ZGRN16RefTempSubobject2srE_ =
// CIR-SAME: #cir.const_record<{#cir.global_view<@_ZGRN16RefTempSubobject2srE_, [1 : i32]> : !cir.ptr<!s32i>,
// CIR-SAME: #cir.const_array<[#cir.int<1> : !s32i, #cir.int<2> : !s32i, #cir.int<3> : !s32i]> : !cir.array<!s32i x 3>}>

// CIR: cir.global  constant external  @_ZN16RefTempSubobject2srE =
// CIR-SAME: #cir.global_view<@_ZGRN16RefTempSubobject2srE_> : !cir.ptr

// LLVM: @_ZGRN16RefTempSubobject2srE_ = internal global { ptr, [3 x i32] }
// LLVM-SAME: { ptr getelementptr (i8, ptr @_ZGRN16RefTempSubobject2srE_, i64 8),
// LLVM-SAME: [3 x i32] [i32 1, i32 2, i32 3] }
// LLVM: @_ZN16RefTempSubobject2srE = constant ptr @_ZGRN16RefTempSubobject2srE_

// OGCG: @_ZGRN16RefTempSubobject2srE_ = internal global { ptr, [3 x i32] }
// OGCG-SAME: { ptr getelementptr (i8, ptr @_ZGRN16RefTempSubobject2srE_, i64 8),
// OGCG-SAME: [3 x i32] [i32 1, i32 2, i32 3] }
// OGCG: @_ZN16RefTempSubobject2srE = constant ptr @_ZGRN16RefTempSubobject2srE_
