// Verify that a member-level __attribute__((aligned(N))) that exceeds the
// member type's natural i386 ABI alignment is honored in the emitted record
// layout (field offset + overall size), matching OGCG. On i386 the ABI
// alignment of i64/double is 4, so an aligned(8) member must be padded up to
// offset 8; ClangIR previously dropped this padding. On x86_64 the natural
// alignment already is 8, so the layout is unchanged (regression guard).

// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.i386.ll
// RUN: FileCheck --input-file=%t.i386.ll %s --check-prefix=I386
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -emit-llvm %s -o %t.i386.ogcg.ll
// RUN: FileCheck --input-file=%t.i386.ogcg.ll %s --check-prefix=I386-OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.x64.ll
// RUN: FileCheck --input-file=%t.x64.ll %s --check-prefix=X64
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.x64.ogcg.ll
// RUN: FileCheck --input-file=%t.x64.ogcg.ll %s --check-prefix=X64-OGCG

typedef unsigned long long u64;

// i64 member over-aligned to 8 (i386 natural align of i64 is 4).
struct M {
  unsigned short a;
  u64 b __attribute__((aligned(8)));
};

// double member over-aligned to 8 (i386 natural align of double is 4).
struct D {
  unsigned short a;
  double b __attribute__((aligned(8)));
};

struct M gM;
struct D gD;

// I386:      %struct.M = type { i16, [6 x i8], i64 }
// I386:      %struct.D = type { i16, [6 x i8], double }
// I386-OGCG: %struct.M = type { i16, [6 x i8], i64 }
// I386-OGCG: %struct.D = type { i16, [6 x i8], double }

// X64:       %struct.M = type { i16, i64 }
// X64:       %struct.D = type { i16, double }
// X64-OGCG:  %struct.M = type { i16, i64 }
// X64-OGCG:  %struct.D = type { i16, double }
