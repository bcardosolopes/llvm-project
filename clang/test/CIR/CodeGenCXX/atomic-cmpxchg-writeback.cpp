// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// __atomic_compare_exchange was not writing the old value back to
// the expected pointer on cmpxchg failure for non-power-of-2 sizes.

template <unsigned Size>
struct S {
    char data[Size];
};

// 3-byte case: non-power-of-2, requires store to tmp + memcpy of 3 bytes
// CHECK-LABEL: define {{.*}} @_Z18test_cmpxchg_3byte
// CHECK:   cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic
// CHECK:   extractvalue { i32, i1 } %{{.*}}, 0
// CHECK:   extractvalue { i32, i1 } %{{.*}}, 1
// CHECK:   store i32 %{{.*}}, ptr %[[TMP3:.*]],
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 4 %[[TMP3]], i64 3, i1 false)

// OGCG-LABEL: define {{.*}} @_Z18test_cmpxchg_3byte
// OGCG:   cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic
// OGCG:   extractvalue { i32, i1 } %{{.*}}, 0
// OGCG:   extractvalue { i32, i1 } %{{.*}}, 1
// OGCG:   store i32 %{{.*}}, ptr %[[TMP3:.*]],
// OGCG:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 4 %[[TMP3]], i64 3, i1 false)
bool test_cmpxchg_3byte(_Atomic(S<3>)* a, S<3>* expected, S<3> desired) {
    return __c11_atomic_compare_exchange_strong(a, expected, desired, 0, 0);
}

// 4-byte case: power-of-2, direct store to expected pointer
// CHECK-LABEL: define {{.*}} @_Z18test_cmpxchg_4byte
// CHECK:   cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic
// CHECK:   extractvalue { i32, i1 } %{{.*}}, 0
// CHECK:   extractvalue { i32, i1 } %{{.*}}, 1
// CHECK:   store i32 %{{.*}}, ptr %{{.*}}

// OGCG-LABEL: define {{.*}} @_Z18test_cmpxchg_4byte
// OGCG:   cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic
// OGCG:   extractvalue { i32, i1 } %{{.*}}, 0
// OGCG:   extractvalue { i32, i1 } %{{.*}}, 1
// OGCG:   store i32 %{{.*}}, ptr %{{.*}}
bool test_cmpxchg_4byte(_Atomic(S<4>)* a, S<4>* expected, S<4> desired) {
    return __c11_atomic_compare_exchange_strong(a, expected, desired, 0, 0);
}

// 6-byte case: non-power-of-2, requires store to tmp + memcpy of 6 bytes
// CHECK-LABEL: define {{.*}} @_Z18test_cmpxchg_6byte
// CHECK:   cmpxchg ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}} monotonic monotonic
// CHECK:   extractvalue { i64, i1 } %{{.*}}, 0
// CHECK:   extractvalue { i64, i1 } %{{.*}}, 1
// CHECK:   store i64 %{{.*}}, ptr %[[TMP6:.*]],
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 8 %[[TMP6]], i64 6, i1 false)

// OGCG-LABEL: define {{.*}} @_Z18test_cmpxchg_6byte
// OGCG:   cmpxchg ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}} monotonic monotonic
// OGCG:   extractvalue { i64, i1 } %{{.*}}, 0
// OGCG:   extractvalue { i64, i1 } %{{.*}}, 1
// OGCG:   store i64 %{{.*}}, ptr %[[TMP6:.*]],
// OGCG:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 8 %[[TMP6]], i64 6, i1 false)
bool test_cmpxchg_6byte(_Atomic(S<6>)* a, S<6>* expected, S<6> desired) {
    return __c11_atomic_compare_exchange_strong(a, expected, desired, 0, 0);
}
