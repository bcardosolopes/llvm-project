// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

typedef __builtin_va_list va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

// Small struct (8 bytes) — direct, uses 8 bytes of GR save area
struct Small { int x, y; };

Small test_small(int n, ...) {
    va_list args;
    va_start(args, n);
    Small s = va_arg(args, Small);
    va_end(args);
    return s;
}

// LLVM-LABEL: @_Z10test_smalliz
// LLVM: %[[GR_OFFS_P:.*]] = getelementptr inbounds nuw %{{.*}}, ptr %{{.*}}, i32 0, i32 3
// LLVM: %[[GR_OFFS:.*]] = load i32, ptr %[[GR_OFFS_P]], align 8
// LLVM: %[[USING_STACK:.*]] = icmp sge i32 %[[GR_OFFS]], 0
// LLVM: br i1 %[[USING_STACK]]
// RegSize = 8 for Small struct
// LLVM: add i32 %[[GR_OFFS]], 8
// LLVM: store i32 %{{.*}}, ptr %[[GR_OFFS_P]], align 8
// LLVM: getelementptr inbounds nuw {{.*}}, i32 0, i32 1
// LLVM: load ptr
// StackSize = 8 for Small struct
// LLVM: getelementptr inbounds i8, ptr %{{.*}}, i64 8
// Final direct copy of the struct into the return temporary.
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 8 %{{.*}}, i64 8, i1 false)

// 16-byte struct — direct, uses 16 bytes of GR save area
struct TwoLong { long a, b; };

TwoLong test_two_long(int n, ...) {
    va_list args;
    va_start(args, n);
    TwoLong s = va_arg(args, TwoLong);
    va_end(args);
    return s;
}

// LLVM-LABEL: @_Z13test_two_longiz
// LLVM: %[[GR_OFFS_P2:.*]] = getelementptr inbounds nuw %{{.*}}, ptr %{{.*}}, i32 0, i32 3
// LLVM: %[[GR_OFFS2:.*]] = load i32, ptr %[[GR_OFFS_P2]], align 8
// RegSize = 16 for TwoLong struct
// LLVM: add i32 %[[GR_OFFS2]], 16
// LLVM: store i32 %{{.*}}, ptr %[[GR_OFFS_P2]], align 8
// StackSize = 16 for TwoLong struct
// LLVM: getelementptr inbounds i8, ptr %{{.*}}, i64 16
// Final direct copy of the struct into the return temporary.
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %{{.*}}, ptr align 8 %{{.*}}, i64 16, i1 false)

// Large struct (>16 bytes) — indirect, passed by pointer
struct Large { long a, b, c; };

Large test_large(int n, ...) {
    va_list args;
    va_start(args, n);
    Large s = va_arg(args, Large);
    va_end(args);
    return s;
}

// LLVM-LABEL: @_Z10test_largeiz
// LLVM: %[[GR_OFFS_P3:.*]] = getelementptr inbounds nuw %{{.*}}, ptr %{{.*}}, i32 0, i32 3
// LLVM: %[[GR_OFFS3:.*]] = load i32, ptr %[[GR_OFFS_P3]], align 8
// RegSize = 8 for indirect struct (pointer size)
// LLVM: add i32 %[[GR_OFFS3]], 8
// LLVM: store i32 %{{.*}}, ptr %[[GR_OFFS_P3]], align 8
// StackSize = 8 for indirect struct
// LLVM: getelementptr inbounds i8, ptr %{{.*}}, i64 8
// Indirect record path: load pointer, then copy the full 24-byte object.
// LLVM: %[[PTR:.*]] = load ptr, ptr
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %{{.*}}, ptr align 8 %[[PTR]], i64 24, i1 false)

// FPR (floating-point register) path — double uses __vr_offs/__vr_top
double test_double_fpr(int n, ...) {
    va_list args;
    va_start(args, n);
    double v = va_arg(args, double);
    va_end(args);
    return v;
}

// LLVM-LABEL: @_Z15test_double_fpriz
// Uses VR register bank (field 4 = __vr_offs, field 2 = __vr_top)
// LLVM: getelementptr inbounds nuw %{{.*}}, ptr %{{.*}}, i32 0, i32 4
// RegSize = 16 for FPR
// LLVM: add i32 %{{.*}}, 16
// LLVM: getelementptr inbounds nuw %{{.*}}, ptr %{{.*}}, i32 0, i32 2
// LLVM: load double, ptr
