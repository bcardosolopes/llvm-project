// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// Test atomic read of vector elements. The OMPIRBuilder doesn't support vector
// element types directly, so we atomically load the entire vector and then
// extract the element.

typedef int int2 __attribute__((__vector_size__(8)));
int2 int2x;
int ulv;

// Vector element via subscript operator.
void test_vector_element_read(void) {
#pragma omp atomic read
  ulv = int2x[0];
}

// CIR-LABEL: cir.func {{.*}} @test_vector_element_read()
// CIR:         %[[VECPTR:.*]] = cir.get_global @int2x
// CIR:         %[[INTPTR:.*]] = cir.cast bitcast %[[VECPTR]] : !cir.ptr<!cir.vector<2 x !s32i>> -> !cir.ptr<!u64i>
// CIR:         %[[INTVAL:.*]] = cir.load {{.*}} atomic(relaxed) %[[INTPTR]]
// CIR:         cir.vec.extract

// LLVM-LABEL: define {{.*}} @test_vector_element_read()
// LLVM:         %[[LD:.*]] = load atomic i64, ptr @int2x monotonic, align 8
// LLVM:         store i64 %[[LD]], ptr %{{.*}}, align 8
// LLVM:         %[[VEC:.*]] = load <2 x i32>, ptr %{{.*}}, align 8
// LLVM:         %[[ELEM:.*]] = extractelement <2 x i32> %[[VEC]], i32 0
// LLVM:         store i32 %[[ELEM]], ptr @ulv

// OGCG-LABEL: define {{.*}} @test_vector_element_read()
// OGCG:         %[[LD:.*]] = load atomic i64, ptr @int2x monotonic, align 8
// OGCG:         store i64 %[[LD]], ptr %{{.*}}, align 8
// OGCG:         %[[VEC:.*]] = load <2 x i32>, ptr %{{.*}}, align 8
// OGCG:         %[[ELEM:.*]] = extractelement <2 x i32> %[[VEC]], i32 0
// OGCG:         store i32 %[[ELEM]], ptr @ulv

typedef float float2 __attribute__((ext_vector_type(2)));
float2 float2x;

// Ext-vector element via component access.
void test_ext_vector_element_read(void) {
#pragma omp atomic read
  ulv = float2x.x;
}

// CIR-LABEL: cir.func {{.*}} @test_ext_vector_element_read()
// CIR:         %[[VECPTR2:.*]] = cir.get_global @float2x
// CIR:         %[[INTPTR2:.*]] = cir.cast bitcast %[[VECPTR2]] : !cir.ptr<!cir.vector<2 x !cir.float>> -> !cir.ptr<!u64i>
// CIR:         %[[INTVAL2:.*]] = cir.load {{.*}} atomic(relaxed) %[[INTPTR2]]
// CIR:         cir.vec.extract

// LLVM-LABEL: define {{.*}} @test_ext_vector_element_read()
// LLVM:         %[[LD2:.*]] = load atomic i64, ptr @float2x monotonic, align 8
// LLVM:         store i64 %[[LD2]], ptr %{{.*}}, align 8
// LLVM:         %[[VEC2:.*]] = load <2 x float>, ptr %{{.*}}, align 8
// LLVM:         %[[ELEM2:.*]] = extractelement <2 x float> %[[VEC2]], i64 0
// LLVM:         %[[CONV:.*]] = fptosi float %[[ELEM2]] to i32
// LLVM:         store i32 %[[CONV]], ptr @ulv

// OGCG-LABEL: define {{.*}} @test_ext_vector_element_read()
// OGCG:         %[[LD2:.*]] = load atomic i64, ptr @float2x monotonic, align 8
// OGCG:         store i64 %[[LD2]], ptr %{{.*}}, align 8
// OGCG:         %[[VEC2:.*]] = load <2 x float>, ptr %{{.*}}, align 8
// OGCG:         %[[ELEM2:.*]] = extractelement <2 x float> %[[VEC2]], i64 0
// OGCG:         %[[CONV:.*]] = fptosi float %[[ELEM2]] to i32
// OGCG:         store i32 %[[CONV]], ptr @ulv
