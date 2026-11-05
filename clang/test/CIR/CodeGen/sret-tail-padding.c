// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Verify that struct tail padding is zeroed in sret return and that
// no dead struct load is emitted after sret calls.

typedef struct { double X; double Y; int Z; } PBVTest;

// The struct is {double, double, i32} = 20 bytes data + 4 bytes tail padding.
// The init list should zero the 4-byte tail padding at offset 20.

PBVTest testRetStruct(float X, double Y, int Z) {
  PBVTest T = { X, Y, Z };
  return T;
}

// CIR-LABEL: @testRetStruct
// CIR:         cir.libc.memset
// CIR:         cir.return

// LLVM-LABEL: define {{.*}} @testRetStruct
// LLVM:         getelementptr inbounds i8, ptr %{{.*}}, i64 20
// LLVM-NEXT:    call void @llvm.memset.p0.i64(ptr %{{.*}}, i8 0, i64 4, i1 false)
// LLVM:         ret void

// OGCG-LABEL: define {{.*}} @testRetStruct
// OGCG:         getelementptr {{.*}}i8, ptr %{{.*}}, i64 20
// OGCG-NEXT:    call void @llvm.memset.p0.i64(ptr {{.*}}, i8 0, i64 4, i1 false)
// OGCG:         ret void

PBVTest testRetStruct2(void);  // external

double CallRetStruct(float X, double Y, int Z) {
  PBVTest T = testRetStruct2();
  return T.X+X+Y+Z;
}

// No dead struct load after sret call — the callee writes directly to the
// local alloca passed as sret, so loading the entire struct is unnecessary.

// LLVM-LABEL: define {{.*}} @CallRetStruct
// LLVM:         call void @testRetStruct2(ptr {{.*}} sret(%struct.PBVTest) {{.*}})
// LLVM-NOT:     load %struct.PBVTest
// LLVM:         getelementptr inbounds

// OGCG-LABEL: define {{.*}} @CallRetStruct
// OGCG:         call void @testRetStruct2(ptr {{.*}} sret(%struct.PBVTest) {{.*}})
// OGCG-NOT:     load %struct.PBVTest
// OGCG:         getelementptr inbounds
