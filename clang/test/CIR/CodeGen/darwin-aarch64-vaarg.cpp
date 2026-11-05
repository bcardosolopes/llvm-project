// RUN: %clang_cc1 -triple arm64-apple-macosx -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test Darwin AArch64 va_arg lowering. Darwin uses a simple char* va_list
// unlike AAPCS which uses a 5-field struct.

typedef __builtin_va_list va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

int test_int(int n, ...) {
    va_list args;
    va_start(args, n);
    int v = va_arg(args, int);
    va_end(args);
    return v;
}

// CHECK-LABEL: @_Z8test_intiz
// Darwin va_list is char*: load ptr, advance by 8, store back, load value
// CHECK: %[[CUR:.*]] = load ptr, ptr %{{.*}}
// CHECK: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i64 8
// CHECK: store ptr %[[NEXT]], ptr %{{.*}}
// CHECK: load i32, ptr %[[CUR]]

double test_double(int n, ...) {
    va_list args;
    va_start(args, n);
    double v = va_arg(args, double);
    va_end(args);
    return v;
}

// CHECK-LABEL: @_Z11test_doubleiz
// CHECK: %[[CUR2:.*]] = load ptr, ptr %{{.*}}
// CHECK: %[[NEXT2:.*]] = getelementptr inbounds i8, ptr %[[CUR2]], i64 8
// CHECK: store ptr %[[NEXT2]], ptr %{{.*}}
// CHECK: load double, ptr %[[CUR2]]

void *test_pointer(int n, ...) {
    va_list args;
    va_start(args, n);
    void *v = va_arg(args, void *);
    va_end(args);
    return v;
}

// CHECK-LABEL: @_Z12test_pointeriz
// CHECK: %[[CUR3:.*]] = load ptr, ptr %{{.*}}
// CHECK: %[[NEXT3:.*]] = getelementptr inbounds i8, ptr %[[CUR3]], i64 8
// CHECK: store ptr %[[NEXT3]], ptr %{{.*}}
// CHECK: load ptr, ptr %[[CUR3]]

struct S {
    __int128 x;
};

S test_overaligned_struct(int n, ...) {
    va_list args;
    va_start(args, n);
    S v = va_arg(args, S);
    va_end(args);
    return v;
}

// CHECK-LABEL: @_Z23test_overaligned_structiz
// CHECK: %[[CUR4:.*]] = load ptr, ptr %{{.*}}
// CHECK: %[[ROUND4:.*]] = getelementptr inbounds i8, ptr %[[CUR4]], i64 15
// CHECK: %[[ALIGNED4:.*]] = call ptr @llvm.ptrmask.p0.i64(ptr %[[ROUND4]], i64 -16)
// CHECK: %[[NEXT4:.*]] = getelementptr inbounds i8, ptr %[[ALIGNED4]], i64 16
// CHECK: store ptr %[[NEXT4]], ptr %{{.*}}
// CHECK: load %struct.S, ptr %[[ALIGNED4]], align 16
