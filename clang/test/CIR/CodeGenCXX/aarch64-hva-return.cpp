// RUN: %clang_cc1 -mfloat-abi hard -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm -o %t.cir.ll %s
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -mfloat-abi hard -triple aarch64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify that HVA (Homogeneous Vector Aggregate) structs are returned directly
// in SIMD/FP registers on AArch64, not via sret. The AArch64 ABI allows direct
// return even for non-trivial C++ types if they are HVAs.

typedef double V __attribute((ext_vector_type(2)));

struct base { V v; };
struct test : base { test(double); protected: V v2; };

// Despite user-defined ctor and protected members, this is an HVA and
// should be returned directly.
test f(test *x) { return *x; }

// LLVM: define dso_local %struct.test @_Z1fP4test(ptr noundef %{{.*}})
// OGCG: define dso_local %struct.test @_Z1fP4test(ptr noundef %{{.*}})

// Regular HFA (doubles) passed and returned directly.
struct hfa { double a, b, c; };
hfa g(hfa x) { return x; }

// LLVM: define dso_local %struct.hfa @_Z1g3hfa([3 x double] alignstack(8) %{{.*}})
// OGCG: define dso_local %struct.hfa @_Z1g3hfa([3 x double] alignstack(8) %{{.*}})

// Non-homogeneous aggregate should use sret.
struct mixed { int x; double y, z; };
mixed h(mixed *x) { return *x; }

// LLVM: define dso_local void @_Z1hP5mixed(ptr dead_on_unwind noalias writable sret(%struct.mixed) align 8 %{{.*}}, ptr
// OGCG: define dso_local void @_Z1hP5mixed(ptr dead_on_unwind noalias writable sret(%struct.mixed) align 8 %{{.*}}, ptr
