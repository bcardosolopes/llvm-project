// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify nested struct HFA detection on AArch64.
// {Inner{float, float}, Inner{float, float}} = 4 floats = HFA.

struct Inner { float x, y; };
struct Outer { Inner a, b; };

// CHECK: void @_Z9use_outer5Outer([4 x float] alignstack(8) %{{.*}})
void use_outer(Outer o) {}

struct Vec3d { double x, y, z; };

// CHECK: void @_Z8use_vec35Vec3d([3 x double] alignstack(8) %{{.*}})
void use_vec3(Vec3d v) {}
