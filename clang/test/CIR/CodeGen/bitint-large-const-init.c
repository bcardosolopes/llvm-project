// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s
//
// Regression test: ConstantAggregateBuilder assertion on structs
// with large _BitInt members. Large _BitInt types (>128 bits) require byte-
// array [N x i8] memory representation in struct field layout when the LLVM
// alloc size of the storage-width integer differs from the AST type size.

struct S_257 {
  _BitInt(257) A;
  int B;
};

struct S_129 {
  _BitInt(17) A;
  _BitInt(129) B;
};

// Test constant initialization with _BitInt(257) struct
_BitInt(257) const_init_257(void) {
  struct S_257 A = {1, 10000};
  return A.A;
}

// CIR-DAG: %struct.S_257 = type { [40 x i8], i32, [4 x i8] }
// CIR-DAG: %struct.S_129 = type { i32, [4 x i8], [24 x i8] }
// OGCG-DAG: %struct.S_257 = type { [40 x i8], i32, [4 x i8] }
// OGCG-DAG: %struct.S_129 = type { i32, [4 x i8], [24 x i8] }

// CIR-LABEL: define {{.*}} void @const_init_257(
// CIR: %[[ALLOCA:.+]] = alloca %struct.S_257, i64 1, align 8
// CIR: call void @llvm.memset{{.*}}(ptr align 8 %[[ALLOCA]], i8 0, i64 48, i1 false)
// CIR: %[[GEP0:.+]] = getelementptr inbounds {{.+}}, ptr %[[ALLOCA]], i32 0, i32 0
// CIR: %[[BYTE0:.+]] = getelementptr inbounds {{.*}}i8, ptr %[[GEP0]], i32 0
// CIR: store i8 1, ptr %[[BYTE0]], align 8
// CIR: %[[GEP1:.+]] = getelementptr inbounds {{.+}}, ptr %[[ALLOCA]], i32 0, i32 1
// CIR: store i32 10000, ptr %[[GEP1]], align 8
// CIR: %[[GEP:.+]] = getelementptr inbounds nuw %struct.S_257, ptr %[[ALLOCA]], i32 0, i32 0
// CIR: %[[LOAD:.+]] = load i320, ptr %[[GEP]], align 8
// CIR: %[[TRUNC:.+]] = trunc i320 %[[LOAD]] to i257

// OGCG-LABEL: define {{.*}} void @const_init_257(
// OGCG: %[[A:.+]] = alloca %struct.S_257, align 8
// OGCG: call void @llvm.memset{{.*}}(ptr align 8 %[[A]], i8 0, i64 48, i1 false)
// OGCG: %[[GEP:.+]] = getelementptr inbounds nuw %struct.S_257, ptr %[[A]], i32 0, i32 0
// OGCG: %[[LOAD:.+]] = load i320, ptr %[[GEP]], align 8
// OGCG: %[[TRUNC:.+]] = trunc i320 %[[LOAD]] to i257

// Test constant initialization with _BitInt(129) struct
int const_init_129(void) {
  struct S_129 A = {1, 170};
  return (int)A.B;
}

// CIR-LABEL: define {{.*}} i32 @const_init_129(
// CIR: %[[ALLOCA:.+]] = alloca %struct.S_129, i64 1, align 8
// CIR: call void @llvm.memcpy{{.*}}(ptr align 8 %[[ALLOCA]], ptr align 8
// CIR: %[[GEP:.+]] = getelementptr inbounds nuw %struct.S_129, ptr %[[ALLOCA]], i32 0, i32 2
// CIR: %[[LOAD:.+]] = load i192, ptr %[[GEP]], align 8
// CIR: %[[TRUNC:.+]] = trunc i192 %[[LOAD]] to i129
// CIR: trunc i129 %[[TRUNC]] to i32

// OGCG-LABEL: define {{.*}} i32 @const_init_129(
// OGCG: %[[GEP:.+]] = getelementptr inbounds nuw %struct.S_129, ptr %{{.+}}, i32 0, i32 2
// OGCG: %[[LOAD:.+]] = load i192, ptr %[[GEP]], align 8
// OGCG: %[[TRUNC:.+]] = trunc i192 %[[LOAD]] to i129

// Test non-constant initialization with _BitInt(129) struct
int nonconst_init_129(int a) {
  struct S_129 B = {1, a};
  return (int)B.B;
}

// CIR-LABEL: define {{.*}} i32 @nonconst_init_129(
// CIR: %[[GEP:.+]] = getelementptr inbounds nuw %struct.S_129, ptr %{{.+}}, i32 0, i32 2
// CIR: store i192 %{{.+}}, ptr %[[GEP]], align 8
// CIR: %[[GEP2:.+]] = getelementptr inbounds nuw %struct.S_129, ptr %{{.+}}, i32 0, i32 2
// CIR: %[[LOAD:.+]] = load i192, ptr %[[GEP2]], align 8
// CIR: trunc i192 %[[LOAD]] to i129

// OGCG-LABEL: define {{.*}} i32 @nonconst_init_129(
// OGCG: %[[GEP:.+]] = getelementptr inbounds nuw %struct.S_129, ptr %{{.+}}, i32 0, i32 2
// OGCG: store i192 %{{.+}}, ptr %[[GEP]], align 8

// Test pointer arithmetic with _BitInt(129)
_BitInt(129) *bitint_ptr_arith(_BitInt(129) *p) {
  return p + 1;
}

// CIR-LABEL: define {{.*}} ptr @bitint_ptr_arith(
// CIR: getelementptr inbounds [24 x i8], ptr %{{.+}}, i64 1

// OGCG-LABEL: define {{.*}} ptr @bitint_ptr_arith(
// OGCG: getelementptr inbounds [24 x i8], ptr %{{.+}}, i64 1
