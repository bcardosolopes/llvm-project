// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify __int128 function arguments and returns use i128 directly,
// matching OGCG's x86-64 ABI.

// CHECK: define dso_local noundef i128 @_Z6add128nn(i128 noundef %{{.*}}, i128 noundef %{{.*}})
// OGCG: define dso_local noundef i128 @_Z6add128nn(i128 noundef %{{.*}}, i128 noundef %{{.*}})
__int128 add128(__int128 a, __int128 b) {
  return a + b;
}

// CHECK: define dso_local noundef i32 @_Z8use_int5v()
// CHECK:   call noundef i128 @_Z6add128nn(i128 noundef
// OGCG: define dso_local noundef i32 @_Z8use_int5v()
// OGCG:   call noundef i128 @_Z6add128nn(i128 noundef
int use_int5() {
  __int128 x = add128(1, 2);
  return (int)x;
}
