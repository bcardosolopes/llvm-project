// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Regression test: large _BitInt arrays must use the ABI storage
// object ([24 x i8] for _BitInt(129) on x86_64), not the widened storage
// integer (i192). Otherwise array element addressing advances by the LLVM
// alloc size of i192 (32 bytes) instead of the 24-byte ABI object size.

_BitInt(129) arr[2];

_BitInt(129) *f(void) { return &arr[1]; }

void take(_BitInt(129) *);
void g(void) {
  _BitInt(129) local[2];
  take(&local[1]);
}

// CIR: @arr = global [2 x [24 x i8]] zeroinitializer, align 16
// OGCG: @arr = global [2 x [24 x i8]] zeroinitializer, align 16

// CIR-LABEL: define dso_local ptr @f()
// CIR: ret ptr getelementptr inbounds{{.*}}(i8, ptr @arr, i64 24)

// OGCG-LABEL: define dso_local ptr @f()
// OGCG: ret ptr getelementptr inbounds ([2 x [24 x i8]], ptr @arr, i64 0, i64 1)

// CIR-LABEL: define dso_local void @g()
// CIR: %[[LOCAL:.+]] = alloca [2 x [24 x i8]], i64 1, align 16
// CIR: %[[ELEM:.+]] = getelementptr inbounds [2 x [24 x i8]], ptr %[[LOCAL]], i{{32|64}} 0, i64 1
// CIR: call void @take(ptr noundef %[[ELEM]])

// OGCG-LABEL: define dso_local void @g()
// OGCG: %[[LOCAL:.+]] = alloca [2 x [24 x i8]], align 16
// OGCG: %[[ELEM:.+]] = getelementptr inbounds [2 x [24 x i8]], ptr %[[LOCAL]], i64 0, i64 1
// OGCG: call void @take(ptr noundef %[[ELEM]])
