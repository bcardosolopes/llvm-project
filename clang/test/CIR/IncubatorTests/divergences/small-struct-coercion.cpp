// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Small structs (8 bytes or less) are coerced to integer types per x86_64 SysV ABI.
// Two-int struct (8 bytes) → i64, single-int struct (4 bytes) → i32.
// Coercion applies to both return values and parameters.

struct SmallStruct {
    int a, b;  // 8 bytes total
};

SmallStruct return_small() {
    return {1, 2};
}

int take_small(SmallStruct s) {
    return s.a + s.b;
}

struct TinyStruct {
    int x;  // 4 bytes
};

TinyStruct return_tiny() {
    return {42};
}

struct LongStruct {
    long long value;
};

LongStruct return_long() {
    return {123456789LL};
}

// CIR: define dso_local i64 @_Z12return_smallv()
// CIR: define dso_local noundef i32 @_Z10take_small11SmallStruct(i64
// CIR: define dso_local i32 @_Z11return_tinyv()
// CIR: define dso_local i64 @_Z11return_longv()

// OGCG: define dso_local i64 @_Z12return_smallv()
// OGCG: define dso_local noundef i32 @_Z10take_small11SmallStruct(i64
// OGCG: define dso_local i32 @_Z11return_tinyv()
// OGCG: define dso_local i64 @_Z11return_longv()
