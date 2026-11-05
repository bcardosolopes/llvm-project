// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.og.ll

// A constant DesignatedInitUpdateExpr that patches an array subobject sitting
// after a bitfield takes the byte-packing update path. The updated field starts
// at a non-zero byte offset (byte 1, after the bitfield storage), so the
// ConstantAggregateBuilder condenses the array from a non-zero start offset.
// This must not shift the array bytes; the emitted constant must match classic
// CodeGen (OGCG) exactly. Regression test for a +1 byte shift where the array
// condense used absolute element offsets instead of offsets relative to the
// subobject's start.

struct Mid {
  int bf : 3;
  char L[6];
};
Mid a[] = { [0] = Mid{ 2, "abc" }, [0].L[4] = 'y' };

// LLVM: @a = {{.*}}[%struct.Mid { i8 2, [6 x i8] c"abc\00y\00", i8 undef }]
// OGCG: @a = {{.*}}[%struct.Mid { i8 2, [6 x i8] c"abc\00y\00", i8 undef }]

// A deeper nest: the update reaches an array (Deep::s) two levels down, through
// a bitfield-preceded record field (Mid2::d at byte 1). Both the inner array
// condense and the outer record condense start at a non-zero offset.

struct Deep {
  char s[6];
  int n;
};
struct Mid2 {
  int bf : 3;
  Deep d;
};
Mid2 b[] = { [0] = Mid2{ 5, {"xyz", 9} }, [0].d.s[3] = 'Q' };

// LLVM: @b = {{.*}}[%struct.Mid2 { i8 5, %struct.Deep { [6 x i8] c"xyzQ\00\00", i32 9 } }]
// OGCG: @b = {{.*}}[%struct.Mid2 { i8 5, %struct.Deep { [6 x i8] c"xyzQ\00\00", i32 9 } }]

// Update on a non-first array field after a bitfield (patch B, keep A).

struct R1 {
  int bf : 5;
  char A[4];
  char B[4];
};
R1 c[] = { [0] = R1{ 1, "pq", "rs" }, [0].B[2] = 'Z' };

// LLVM: @c = {{.*}}[%struct.R1 { i8 1, [4 x i8] c"pq\00\00", [4 x i8] c"rsZ\00", [3 x i8] undef }]
// OGCG: @c = {{.*}}[%struct.R1 { i8 1, [4 x i8] c"pq\00\00", [4 x i8] c"rsZ\00", [3 x i8] undef }]
