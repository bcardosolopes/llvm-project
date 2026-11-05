// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify _Float16 and __bf16 are passed/returned in SSE registers,
// matching OGCG behavior.

// CHECK: define {{.*}} noundef half @_Z8add_fp16DF16_DF16_(half noundef %{{.*}}, half noundef %{{.*}})
// OGCG: define {{.*}} half @_Z8add_fp16DF16_DF16_(half {{.*}}, half {{.*}})
_Float16 add_fp16(_Float16 a, _Float16 b) { return a + b; }

// CHECK: define {{.*}} noundef bfloat @_Z13identity_bf16DF16b(bfloat noundef %{{.*}})
// OGCG: define {{.*}} bfloat @_Z13identity_bf16DF16b(bfloat {{.*}})
__bf16 identity_bf16(__bf16 x) { return x; }
