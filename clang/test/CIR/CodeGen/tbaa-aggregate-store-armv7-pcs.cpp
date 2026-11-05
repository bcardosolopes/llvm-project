// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabi -target-feature +vfp3 -mfloat-abi hard -fclangir -O2 -emit-llvm %s -o - | FileCheck %s

// On armv7 hard-float the AAPCS-VFP calling convention returns a small
// homogeneous-float struct by value, which flows through the scalar store path
// as a whole `%struct.S` store. CIR attached a TBAA access tag whose access type
// was the struct's (base-type) node; the LLVM verifier rejects that at -O2/-O3
// ("Access type node must be a valid scalar type"), aborting with "Broken module
// found". A whole aggregate has no valid scalar access type, so no access tag
// must be emitted for it (classic CodeGen uses !tbaa.struct for aggregate
// copies instead). This must now compile to a valid module.

struct S { float a, b, c, d; };
S make() { S s = {1, 2, 3, 4}; return s; }
float use() { S s = make(); return s.a + s.d; }

// CHECK: define{{.*}} %struct.S @_Z4makev
// CHECK: define{{.*}} float @_Z3usev
