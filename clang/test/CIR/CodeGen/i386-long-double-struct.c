// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --check-prefix=OGCG < %t.ogcg.ll

// Verify that long double (x86_fp80) on i386 uses 4-byte alignment (not 16).
// On i386, the data layout has f80:32, meaning 4-byte ABI alignment and
// allocSize = alignTo(10, 4) = 12 bytes.
//
// Previously, FP80Type hardcoded 16-byte alignment and 128-bit size, which
// produced allocSize=16 on all targets. This caused an assertion failure in
// checkBitfieldClipping because the member tail (16) exceeded the AST-computed
// data size (12).
//
//

struct s0 { long double a; };
void f0(struct s0 a) {}

struct s1 { long double a; int b; };
void f1(struct s1 a) {}

struct s2 { int a; long double b; };
void f2(struct s2 a) {}

struct s3 { long double a; long double b; };
void f3(struct s3 a) {}

long double g0;

// Check struct type layouts (appear at top of LLVM output).
// LLVM: %struct.s0 = type { x86_fp80 }
// LLVM: %struct.s1 = type { x86_fp80, i32 }
// LLVM: %struct.s2 = type { i32, x86_fp80 }
// LLVM: %struct.s3 = type { x86_fp80, x86_fp80 }

// OGCG: %struct.s0 = type { x86_fp80 }
// OGCG: %struct.s1 = type { x86_fp80, i32 }
// OGCG: %struct.s2 = type { i32, x86_fp80 }
// OGCG: %struct.s3 = type { x86_fp80, x86_fp80 }

// Verify _Complex long double struct layout has no spurious padding.
// The complex type is [2 x x86_fp80], allocSize = 2 * 12 = 24 bytes on i386.
struct s4 { _Complex long double a; int b; };
void f4(struct s4 a) {}

// LLVM: %struct.s4 = type { { x86_fp80, x86_fp80 }, i32 }
// OGCG: %struct.s4 = type { { x86_fp80, x86_fp80 }, i32 }

// Verify 4-byte alignment for the global.
// LLVM: @g0 = global x86_fp80 {{.*}}, align 4
// OGCG: @g0 = global x86_fp80 {{.*}}, align 4
