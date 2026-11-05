// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

struct S {
  unsigned x : 3;
};

// Pre-increment of a 3-bit unsigned bitfield at max value (7) should
// return the truncated stored value (0), not the pre-truncation value (8).
// Both CIR and OGCG should mask the stored value and return the truncated
// result as the expression value.

int pre_inc_test(S &s) {
  return ++s.x;
}

// LLVM-LABEL: @_Z12pre_inc_test
// LLVM: add i32 %{{.*}}, 1
// LLVM: %[[TRUNC:.*]] = and i8 %{{.*}}, 7
// LLVM: %[[EXT:.*]] = zext i8 %[[TRUNC]] to i32
// LLVM: ret i32 %[[EXT]]

// OGCG-LABEL: @_Z12pre_inc_test
// OGCG: add i32 %{{.*}}, 1
// OGCG: and i8 %{{.*}}, 7
// OGCG: ret i32

// Compound assignment (+=) on a bitfield should also return the truncated
// stored value per C99 6.5.16p1.

int compound_add_test(S &s) {
  return s.x += 5;
}

// LLVM-LABEL: @_Z17compound_add_test
// LLVM: add nsw i32 %{{.*}}, 5
// LLVM: %[[TRUNC2:.*]] = and i8 %{{.*}}, 7
// LLVM: %[[EXT2:.*]] = zext i8 %[[TRUNC2]] to i32
// LLVM: ret i32 %[[EXT2]]

// OGCG-LABEL: @_Z17compound_add_test
// OGCG: add nsw i32 %{{.*}}, 5
// OGCG: and i8 %{{.*}}, 7
// OGCG: ret i32
