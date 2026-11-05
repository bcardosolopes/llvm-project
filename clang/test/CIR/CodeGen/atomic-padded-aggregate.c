// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.og.ll %s

// Test aggregate expression emission for padded atomic types.
// A 3-byte struct padded to 4 bytes for atomic operations requires
// special handling in CK_AtomicToNonAtomic and CK_NonAtomicToAtomic
// aggregate casts.

typedef struct { char a, b, c; } S3;

S3 test_atomic_load(_Atomic(S3) *p) {
  return *p;
}

// CIR-LABEL: cir.func {{.*}}@test_atomic_load(
// CIR:         cir.alloca {{.*}}, ["atomic-to-nonatomic.temp"]
// CIR:         cir.get_member %{{.*}}[0] {name = ""}
// CIR:         cir.copy

// LLVM-LABEL: define {{.*}}@test_atomic_load(
// LLVM:         alloca { %struct.S3, [1 x i8] }
// LLVM:         getelementptr inbounds nuw { %struct.S3, [1 x i8] }, ptr %{{.*}}, i32 0, i32 0

// OGCG-LABEL: define {{.*}}@test_atomic_load(
// OGCG:         alloca { %struct.S3, [1 x i8] }
// OGCG:         getelementptr {{.*}}{ %struct.S3, [1 x i8] }, ptr %{{.*}}, i32 0, i32 0

void test_atomic_store(_Atomic(S3) *p, S3 val) {
  *p = val;
}

// CIR-LABEL: cir.func {{.*}}@test_atomic_store(
// CIR:         cir.const #cir.zero
// CIR:         cir.get_member %{{.*}}[0] {name = ""}
// CIR:         cir.copy

// LLVM-LABEL: define {{.*}}@test_atomic_store(
// LLVM:         store { %struct.S3, [1 x i8] } zeroinitializer
// LLVM:         getelementptr inbounds nuw { %struct.S3, [1 x i8] }, ptr %{{.*}}, i32 0, i32 0

// OGCG-LABEL: define {{.*}}@test_atomic_store(
// OGCG:         call void @llvm.memset
// OGCG:         getelementptr {{.*}}{ %struct.S3, [1 x i8] }, ptr %{{.*}}, i32 0, i32 0
