// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fmerge-all-constants -fclangir -emit-llvm -std=c++11 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s

// Static local with self-referencing constexpr pointer creates a
// spurious .1 suffixed zeroinitializer global. The pointer field in the
// real global points to the .1 copy instead of itself.
//
// Expected: @...sntd = internal global { i32 120, ptr @...sntd }
// Actual:   @...sntd.1 = internal global ... zeroinitializer
//           @...sntd = internal global { i32 120, ptr @...sntd.1 }

namespace NonLiteralConstexpr {
  struct NonTrivialDtor {
    constexpr NonTrivialDtor(int n, const NonTrivialDtor *p) : n(n), p(p) {}
    int n;
    const NonTrivialDtor *p;
    ~NonTrivialDtor();
  };

  void StaticVars() {
    static NonTrivialDtor sntd(120, &sntd);
  }
}

// The static local must point to itself, not to a spurious .1 copy.
// CHECK: @_ZZN19NonLiteralConstexpr10StaticVarsEvE4sntd = internal global
// CHECK-SAME: i32 120
// CHECK-SAME: ptr @_ZZN19NonLiteralConstexpr10StaticVarsEvE4sntd }
// The closing brace ensures we match the exact name without a .1 suffix.
// Also there should be no .1 copy:
// CHECK-NOT: @_ZZN19NonLiteralConstexpr10StaticVarsEvE4sntd.1
