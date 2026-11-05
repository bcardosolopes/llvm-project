// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.og.ll

// Test array-type InitListExpr constant emission and DesignatedInitUpdateExpr
// with string literal bases (both wide and narrow strings).

// Basic: array of structs with designated init update on wide string member.
struct WideStr {
  __typeof(L"foo"[0]) L[6];
  int M;
};
struct WideStr ws[] = { { { L"foo" }, 1 }, [0].L[2] = L'x'};

// CIR: cir.global  external  @ws
// CIR-SAME: #cir.int<102>
// CIR-SAME: #cir.int<111>
// CIR-SAME: #cir.int<120>

// LLVM: @ws = global [1 x %struct.WideStr] [%struct.WideStr { [6 x i32] [i32 102, i32 111, i32 120, i32 0, i32 0, i32 0], i32 1 }]
// OGCG: @ws = global [1 x %struct.WideStr] [%struct.WideStr { [6 x i32] [i32 102, i32 111, i32 120, i32 0, i32 0, i32 0], i32 1 }]

// Narrow string with designated init update patching beyond string length.
struct NarrowStr {
  char L[6];
  int M;
};
struct NarrowStr ns[] = { { { "foo" }, 1 }, [0].L[4] = 'y'};

// LLVM: @ns = global {{.*}} c"foo\00y\00"
// OGCG: @ns = global {{.*}} c"foo\00y\00"

// Narrow string with designated init update within string content.
struct NarrowStr2 {
  char L[6];
  int M;
};
struct NarrowStr2 ns2[] = { { { "foo" }, 1 }, [0].L[2] = 'x'};

// LLVM: @ns2 = global {{.*}} c"fox\00\00\00"
// OGCG: @ns2 = global {{.*}} c"fox\00\00\00"

// ZeroAttr base with designated init update — tests the sparse
// array path in VisitDesignatedInitUpdateExpr.
struct ZeroBase {
  int arr[100];
};
struct ZeroBase zb = {{}, .arr[50] = 42};

// LLVM: @zb = global {{.*}} i32 42
// OGCG: @zb = global {{.*}} i32 42
