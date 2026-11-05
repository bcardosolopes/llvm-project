// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// A packed struct whose trailing `double` sits at offset 4 (not a multiple of
// its natural 8-byte alignment) must be classified MEMORY per AMD64-ABI
// 3.2.3p2 Rule 1 (unaligned field), i.e. passed byval / returned sret with the
// full 12 bytes copied -- NOT coerced to a single i64, which would drop the
// upper half of `m2`.
//
// The unnamed zero-width bitfield `:0` has the same AST offset (32 bits) as the
// following `double m2`. A prior CIR bug recorded that offset as bitfield
// storage, so the double was mistaken for a bitfield and its unaligned-field
// MEMORY check was skipped.

struct S0 {
  unsigned int m0 : 3;
  unsigned int m1 : 7;
  unsigned int : 0;
  double m2;
} __attribute__((packed, aligned(4)));

struct S0 rt_S0(struct S0 v) { return v; }

// CHECK: define {{.*}} @rt_S0(ptr {{.*}} sret(%struct.S0) align 4 %{{.*}}, ptr {{.*}} byval(%struct.S0) align 8 %{{.*}})
// CHECK-NOT: define {{.*}} i64 @rt_S0(i64
// OGCG: define {{.*}} @rt_S0(ptr {{.*}} sret(%struct.S0) align 4 %{{.*}}, ptr {{.*}} byval(%struct.S0) align 8 %{{.*}})
