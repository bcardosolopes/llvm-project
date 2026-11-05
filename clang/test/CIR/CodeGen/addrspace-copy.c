// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s -check-prefix=OGCG

// Test that cir.copy works correctly with different address spaces.

typedef struct { int a; } MyStruct;
void test(MyStruct __attribute__((address_space(2))) *pPtr) {
  MyStruct s = pPtr[0];
  pPtr[0] = s;
}

// CIR: cir.copy %{{.*}} to %{{.*}} {{.*}} : !cir.ptr<!rec_MyStruct, target_address_space(2)>, !cir.ptr<!rec_MyStruct>
// CIR: cir.copy %{{.*}} to %{{.*}} {{.*}} : !cir.ptr<!rec_MyStruct>, !cir.ptr<!rec_MyStruct, target_address_space(2)>

// LLVM: call void @llvm.memcpy.p0.p2.i64(ptr {{.*}}, ptr addrspace(2) {{.*}}, i64 4, i1 false)
// LLVM: call void @llvm.memcpy.p2.p0.i64(ptr addrspace(2) {{.*}}, ptr {{.*}}, i64 4, i1 false)

// OGCG: call void @llvm.memcpy.p0.p2.i64(ptr {{.*}}, ptr addrspace(2) {{.*}}, i64 4, i1 false)
// OGCG: call void @llvm.memcpy.p2.p0.i64(ptr addrspace(2) {{.*}}, ptr {{.*}}, i64 4, i1 false)
