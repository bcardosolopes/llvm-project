// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that bool bitfields are correctly initialized through both the
// InitListExpr path and the APValue constant evaluation path.

struct BoolBitfield {
  bool b : 1;
  int x : 7;
};

// InitListExpr path: b=1, x=7 → 0b00001111 = 15
// CHECK: @g1 = global %struct.BoolBitfield { i8 15,
// OGCG: @g1 = global %struct.BoolBitfield { i8 15,
BoolBitfield g1 = {true, 7};

// APValue path (constexpr init copied to non-constexpr global):
// b=1, x=42 → 0b01010101 = 85
constexpr BoolBitfield bf_val = {true, 42};
// CHECK: @g2 = global %struct.BoolBitfield { i8 85,
// OGCG: @g2 = global %struct.BoolBitfield { i8 85,
BoolBitfield g2 = bf_val;

// All-zero case
// CHECK: @g3 = global %struct.BoolBitfield
// OGCG: @g3 = global %struct.BoolBitfield
BoolBitfield g3 = {false, 0};

// Multiple bool bitfields in a single byte:
// a=1, b=0, c=1, pad=0 → 0b00000101 = 5
struct MultiBool {
  bool a : 1;
  bool b : 1;
  bool c : 1;
  int pad : 5;
};

// CHECK: @g4 = global %struct.MultiBool { i8 5,
// OGCG: @g4 = global %struct.MultiBool { i8 5,
MultiBool g4 = {true, false, true, 0};
