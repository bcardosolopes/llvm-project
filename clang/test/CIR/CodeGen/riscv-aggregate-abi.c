// RUN: %clang_cc1 -fclangir -triple riscv64-unknown-linux-gnu -target-feature +d -target-abi lp64d -emit-llvm %s -o %t.rv64.ll
// RUN: FileCheck %s --input-file=%t.rv64.ll --check-prefix=RV64
// RUN: %clang_cc1 -triple riscv64-unknown-linux-gnu -target-feature +d -target-abi lp64d -emit-llvm %s -o %t.rv64.ogcg.ll
// RUN: FileCheck %s --input-file=%t.rv64.ogcg.ll --check-prefix=RV64

// RUN: %clang_cc1 -fclangir -triple riscv32-unknown-linux-gnu -target-feature +d -target-abi ilp32d -emit-llvm %s -o %t.rv32.ll
// RUN: FileCheck %s --input-file=%t.rv32.ll --check-prefix=RV32
// RUN: %clang_cc1 -triple riscv32-unknown-linux-gnu -target-feature +d -target-abi ilp32d -emit-llvm %s -o %t.rv32.ogcg.ll
// RUN: FileCheck %s --input-file=%t.rv32.ogcg.ll --check-prefix=RV32

// Test that the RISC-V target ABI is implemented under CIR and byte-matches
// classic CodeGen (OGCG) for the hard-float (lp64d / ilp32d) calling
// convention. The same RUN lines check both -fclangir and classic against a
// single prefix per ABI, so the two must agree exactly.

struct S1 { int a; };
struct S2 { int a; int b; };
struct S3 { int a; int b; int c; };
struct Sd { double d; };
struct Sf { float f; };
struct Sff { float f; float g; };
struct Sfi { float f; int i; };
struct Sdd { double d; double e; };
struct Sbig { long a; long b; long c; };
struct E {};

// Single-int struct: coerced to one XLEN integer.
// RV64: define {{.*}}i64 @mk1(i32 {{.*}})
// RV64: define {{.*}}i32 @u1(i64 {{.*}})
// RV32: define {{.*}}i32 @mk1(i32 {{.*}})
// RV32: define {{.*}}i32 @u1(i32 {{.*}})
struct S1 mk1(int x) { struct S1 s = {x}; return s; }
int u1(struct S1 s) { return s.a; }

// Two-int struct (2*XLEN on RV32, XLEN on RV64): RV64 uses a single i64, RV32
// uses a two-element [2 x i32] array.
// RV64: define {{.*}}i64 @mk2(i32 {{.*}})
// RV64: define {{.*}}i32 @u2(i64 {{.*}})
// RV32: define {{.*}}[2 x i32] @mk2(i32 {{.*}})
// RV32: define {{.*}}i32 @u2([2 x i32] {{.*}})
struct S2 mk2(int x) { struct S2 s = {x, x}; return s; }
int u2(struct S2 s) { return s.a + s.b; }

// Three-int struct (96 bits): <= 2*XLEN on RV64 -> [2 x i64]; > 2*XLEN on
// RV32 -> indirect (sret / byval).
// RV64: define {{.*}}[2 x i64] @mk3(i32 {{.*}})
// RV64: define {{.*}}i32 @u3([2 x i64] {{.*}})
// RV32: define {{.*}}void @mk3(ptr {{.*}}sret(%struct.S3) {{.*}}, i32 {{.*}})
// RV32: define {{.*}}i32 @u3(ptr {{.*}})
struct S3 mk3(int x) { struct S3 s = {x, x, x}; return s; }
int u3(struct S3 s) { return s.a; }

// Single-double struct: passed/returned in one FP register.
// RV64: define {{.*}}double @mkd(double {{.*}})
// RV64: define {{.*}}double @ud(double {{.*}})
// RV32: define {{.*}}double @mkd(double {{.*}})
// RV32: define {{.*}}double @ud(double {{.*}})
struct Sd mkd(double x) { struct Sd s = {x}; return s; }
double ud(struct Sd s) { return s.d; }

// Single-float struct: passed/returned in one FP register.
// RV64: define {{.*}}float @mkf(float {{.*}})
// RV64: define {{.*}}float @uf(float {{.*}})
// RV32: define {{.*}}float @mkf(float {{.*}})
// RV32: define {{.*}}float @uf(float {{.*}})
struct Sf mkf(float x) { struct Sf s = {x}; return s; }
float uf(struct Sf s) { return s.f; }

// float + float: flattened to two FP registers (CoerceAndExpand), same on both
// ABIs since FLen == 64.
// RV64: define {{.*}}{ float, float } @mkff(float {{.*}})
// RV64: define {{.*}}float @uff(float {{.*}}, float {{.*}})
// RV32: define {{.*}}{ float, float } @mkff(float {{.*}})
// RV32: define {{.*}}float @uff(float {{.*}}, float {{.*}})
struct Sff mkff(float x) { struct Sff s = {x, x}; return s; }
float uff(struct Sff s) { return s.f + s.g; }

// float + int: FP register for the float, GPR for the int.
// RV64: define {{.*}}{ float, i32 } @mkfi(float {{.*}}, i32 {{.*}})
// RV64: define {{.*}}float @ufi(float {{.*}}, i32 {{.*}})
// RV32: define {{.*}}{ float, i32 } @mkfi(float {{.*}}, i32 {{.*}})
// RV32: define {{.*}}float @ufi(float {{.*}}, i32 {{.*}})
struct Sfi mkfi(float x, int y) { struct Sfi s = {x, y}; return s; }
float ufi(struct Sfi s) { return s.f; }

// double + double: flattened to two FP registers.
// RV64: define {{.*}}{ double, double } @mkdd(double {{.*}})
// RV64: define {{.*}}double @udd(double {{.*}}, double {{.*}})
// RV32: define {{.*}}{ double, double } @mkdd(double {{.*}})
// RV32: define {{.*}}double @udd(double {{.*}}, double {{.*}})
struct Sdd mkdd(double x) { struct Sdd s = {x, x}; return s; }
double udd(struct Sdd s) { return s.d + s.e; }

// Large struct (> 2*XLEN): indirect sret return + byval-style indirect arg.
// RV64: define {{.*}}void @mkbig(ptr {{.*}}sret(%struct.Sbig) {{.*}}, i64 {{.*}})
// RV64: define {{.*}}i64 @ubig(ptr {{.*}})
// RV32: define {{.*}}void @mkbig(ptr {{.*}}sret(%struct.Sbig) {{.*}}, i32 {{.*}})
// RV32: define {{.*}}i32 @ubig(ptr {{.*}})
struct Sbig mkbig(long x) { struct Sbig s = {x, x, x}; return s; }
long ubig(struct Sbig s) { return s.a; }

// Empty struct: ignored as an argument; void return.
// RV64: define {{.*}}i32 @ue()
// RV64: define {{.*}}void @me()
// RV32: define {{.*}}i32 @ue()
// RV32: define {{.*}}void @me()
int ue(struct E e) { return 7; }
struct E me(void) { struct E e = {}; return e; }
