// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s
// Test that __int128 members cause correct byval/sret alignment (16 bytes).
// Previously, __int128 alignment was capped at LongLongAlign (8 bytes).

struct PtrInt128 { void* p; __int128 i; };
struct PtrInt128 ptr_int128_fn(struct PtrInt128 x) { return x; }

// CHECK: define {{.*}} @ptr_int128_fn(ptr {{.*}} sret(%struct.PtrInt128) align 16 %{{.*}}, ptr {{.*}} byval(%struct.PtrInt128) align 16 %{{.*}})
// OGCG: define {{.*}} @ptr_int128_fn(ptr {{.*}} sret(%struct.PtrInt128) align 16 %{{.*}}, ptr {{.*}} byval(%struct.PtrInt128) align 16 %{{.*}})

// Test that __attribute__((aligned(N))) on struct fields propagates to
// byval/sret alignment. The attribute is on the field, not the struct, so
// the alignment must be extracted from the AST FieldDecl.

struct A32 { __attribute__((aligned(32))) int x; };
struct A32 a32_fn(struct A32 x) { return x; }

// CHECK: define {{.*}} @a32_fn(ptr {{.*}} sret(%struct.A32) align 32 %{{.*}}, ptr {{.*}} byval(%struct.A32) align 32 %{{.*}})
// OGCG: define {{.*}} @a32_fn(ptr {{.*}} sret(%struct.A32) align 32 %{{.*}}, ptr {{.*}} byval(%struct.A32) align 32 %{{.*}})

struct A64 { __attribute__((aligned(64))) int x; };
struct A64 a64_fn(struct A64 x) { return x; }

// CHECK: define {{.*}} @a64_fn(ptr {{.*}} sret(%struct.A64) align 64 %{{.*}}, ptr {{.*}} byval(%struct.A64) align 64 %{{.*}})
// OGCG: define {{.*}} @a64_fn(ptr {{.*}} sret(%struct.A64) align 64 %{{.*}}, ptr {{.*}} byval(%struct.A64) align 64 %{{.*}})
