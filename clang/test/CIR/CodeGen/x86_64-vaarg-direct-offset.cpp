// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG
//
// va_arg with structs containing empty sub-objects where the ABI coercion type
// has a non-zero DirectOffset. This exercises the byte-level GEP in the va_arg
// lowering (LoweringPrepareX86CXXABI.cpp) that stores coerced register values
// at an offset within the temporary struct. Also tests correct alignment for
// stores and loads in the va_arg register path, and empty record handling.

// --- Empty record: va_arg should return undef without touching va_list ---

typedef struct { struct {} a; } empty;

// CIR-LABEL: define {{.*}} @_Z17empty_record_testiz(
// CIR-NOT: gp_offset
// CIR-NOT: fp_offset
// CIR-NOT: overflow_arg_area
// CIR-NOT: reg_save_area
// CIR-NOT: alloca %struct.empty
// CIR-NOT: store %struct.empty
//
// OGCG-LABEL: define {{.*}} @_Z17empty_record_testiz(
// OGCG-NOT: gp_offset
// OGCG-NOT: fp_offset
// OGCG-NOT: overflow_arg_area
// OGCG-NOT: reg_save_area
empty empty_record_test(int z, ...) {
  __builtin_va_list list;
  __builtin_va_start(list, z);
  return __builtin_va_arg(list, empty);
}

// --- Alignment tests ---

// Struct with empty leading member: the double is at offset 8 in the struct,
// so the ABI coercion type (double) has DirectOffset == 8.
typedef struct {
  struct{} a;
  double b;
} S1;

// CIR-LABEL: define {{.*}} @_Z2f1iz(
// CIR: getelementptr inbounds i8, ptr %{{.*}}, i64 8
// CIR: store double %{{.*}}, ptr %{{.*}}, align 8
//
// OGCG-LABEL: define {{.*}} @_Z2f1iz(
// OGCG: getelementptr i8, ptr %{{.*}}, i32 8
// OGCG: store double %{{.*}}, ptr %{{.*}}, align 8
S1 f1(int z, ...) {
  __builtin_va_list list;
  __builtin_va_start(list, z);
  return __builtin_va_arg(list, S1);
}

// Struct with leading empty array and two floats: coerced to <2 x float>
// with DirectOffset == 8. Natural alignment is 4.
typedef struct {
  struct{} a[5];
  float b;
  float c;
} S2;

// CIR-LABEL: define {{.*}} @_Z2f2iz(
// CIR: getelementptr inbounds i8, ptr %{{.*}}, i64 8
// CIR: store <2 x float> %{{.*}}, ptr %{{.*}}, align 4
// CIR: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 16, i1 false)
// CIR: %{{.*}} = getelementptr inbounds i8, ptr %{{.*}}, i64 8
// CIR: %{{.*}} = load <2 x float>, ptr %{{.*}}, align 4
//
// OGCG-LABEL: define {{.*}} @_Z2f2iz(
// OGCG: getelementptr i8, ptr %{{.*}}, i32 8
// OGCG: store <2 x float> %{{.*}}, ptr %{{.*}}, align 4
// OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 16, i1 false)
// OGCG: %{{.*}} = getelementptr inbounds i8, ptr %{{.*}}, i64 8
// OGCG: %{{.*}} = load <2 x float>, ptr %{{.*}}, align 4
S2 f2(int z, ...) {
  __builtin_va_list list;
  __builtin_va_start(list, z);
  return __builtin_va_arg(list, S2);
}

// Struct with trailing empty member: integer at offset 0, DirectOffset == 0.
// This tests that the non-offset path still works.
typedef struct {
  long long a;
  struct{} b;
} S3;

// CIR-LABEL: define {{.*}} @_Z2f3iz(
// CIR: load i64, ptr %{{.*}}, align 8
//
// OGCG-LABEL: define {{.*}} @_Z2f3iz(
// OGCG: load i64, ptr %{{.*}}, align 8
S3 f3(int z, ...) {
  __builtin_va_list list;
  __builtin_va_start(list, z);
  return __builtin_va_arg(list, S3);
}

// Struct with 4-byte alignment passed in GP register with DirectOffset == 8.
// Tests that store and final load both use align 4, not align 8.
typedef struct {
  struct{} a[7];
  short b;
  int c;
} S4;

// CIR-LABEL: define {{.*}} @_Z2f4iz(
// CIR: store i64 %{{.*}}, ptr %{{.*}}, align 4
// CIR: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 16, i1 false)
// CIR: %{{.*}} = getelementptr inbounds i8, ptr %{{.*}}, i64 8
// CIR: %{{.*}} = load i64, ptr %{{.*}}, align 4
//
// OGCG-LABEL: define {{.*}} @_Z2f4iz(
// OGCG: store i64 %{{.*}}, ptr %{{.*}}, align 4
// OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 16, i1 false)
// OGCG: %{{.*}} = getelementptr inbounds i8, ptr %{{.*}}, i64 8
// OGCG: %{{.*}} = load i64, ptr %{{.*}}, align 4
S4 f4(int z, ...) {
  __builtin_va_list list;
  __builtin_va_start(list, z);
  return __builtin_va_arg(list, S4);
}
