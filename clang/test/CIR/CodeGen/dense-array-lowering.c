// RUN: %clang_cc1 -emit-llvm -fclangir -triple x86_64-unknown-linux-gnu %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Verify that constant arrays of primitive types are lowered
// efficiently using DenseElementsAttr (single ConstantDataArray) instead of
// per-element InsertValueOps, and that the output matches OGCG.

// --- i8 arrays ---

// LLVM: @bytes = global [5 x i8] c"\01\02\03\04\05"
// OGCG: @bytes = global [5 x i8] c"\01\02\03\04\05"
unsigned char bytes[] = {1, 2, 3, 4, 5};

// --- large i8 array from integer initializer list ---

// Verify this stays on the dense-constant path and does not build an
// InsertValue chain. This is a regression guard for /.
//
// LLVM: @large_bytes = global [64 x i8] c"\01\02\03\04\05\06\07\08\09\0A\0B\0C\0D\0E\0F\10\11\12\13\14\15\16\17\18\19\1A\1B\1C\1D\1E\1F !\22#$%&'()*+,-./0123456789:;<=>?@"
// LLVM-NOT: insertvalue
// OGCG: @large_bytes = global [64 x i8] c"\01\02\03\04\05\06\07\08\09\0A\0B\0C\0D\0E\0F\10\11\12\13\14\15\16\17\18\19\1A\1B\1C\1D\1E\1F !\22#$%&'()*+,-./0123456789:;<=>?@"
unsigned char large_bytes[64] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,
};

// --- i32 arrays ---

// LLVM: @ints = global [4 x i32] [i32 10, i32 20, i32 30, i32 40]
// OGCG: @ints = global [4 x i32] [i32 10, i32 20, i32 30, i32 40]
int ints[] = {10, 20, 30, 40};

// --- float arrays ---

// LLVM: @floats = global [3 x float] [float 1.000000e+00, float 2.000000e+00, float 3.000000e+00]
// OGCG: @floats = global [3 x float] [float 1.000000e+00, float 2.000000e+00, float 3.000000e+00]
float floats[] = {1.0f, 2.0f, 3.0f};

// --- i32 array with trailing zeros ---

// LLVM: @sparse = global [8 x i32] [i32 42, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0]
// OGCG: @sparse = global [8 x i32] [i32 42, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0]
int sparse[8] = {42};

// --- struct arrays still work (fallback to InsertValueOp chain) ---

struct S { int x; float y; };

// LLVM: @structs = global [2 x %struct.S] [%struct.S { i32 1, float 2.000000e+00 }, %struct.S { i32 3, float 4.000000e+00 }]
// OGCG: @structs = global [2 x %struct.S] [%struct.S { i32 1, float 2.000000e+00 }, %struct.S { i32 3, float 4.000000e+00 }]
struct S structs[] = {{1, 2.0f}, {3, 4.0f}};

// --- float sparse array (packed-struct prefix fast path) ---
// Keep array bound large enough to trigger packed-struct lowering threshold
// (>= 8 trailing zeros), while nonzero prefix is >= 8 to hit DenseElementsAttr
// prefix construction in the packed path.
//
// LLVM: @float_sparse = global <{ [10 x float], [10 x float] }> <{ [10 x float] [float 1.000000e+00, float 2.000000e+00, float 3.000000e+00, float 4.000000e+00, float 5.000000e+00, float 6.000000e+00, float 7.000000e+00, float 8.000000e+00, float 9.000000e+00, float 1.000000e+01], [10 x float] zeroinitializer }>
// OGCG: @float_sparse = global <{ [10 x float], [10 x float] }> <{ [10 x float] [float 1.000000e+00, float 2.000000e+00, float 3.000000e+00, float 4.000000e+00, float 5.000000e+00, float 6.000000e+00, float 7.000000e+00, float 8.000000e+00, float 9.000000e+00, float 1.000000e+01], [10 x float] zeroinitializer }>
float float_sparse[20] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
