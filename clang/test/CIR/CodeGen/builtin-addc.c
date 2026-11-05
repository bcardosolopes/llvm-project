// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

unsigned int test_addc(unsigned int x, unsigned int y, unsigned int carryin,
                       unsigned int *carryout) {
  return __builtin_addc(x, y, carryin, carryout);
}

// CIR-LABEL: @test_addc
// CIR: %[[SUM1:.*]], %[[CARRY1:.*]] = cir.binop.overflow(add, %{{.*}}, %{{.*}}) : !u32i, (!u32i, !cir.bool)
// CIR: %[[SUM2:.*]], %[[CARRY2:.*]] = cir.binop.overflow(add, %[[SUM1]], %{{.*}}) : !u32i, (!u32i, !cir.bool)
// CIR: %{{.*}} = cir.binop(or, %[[CARRY1]], %[[CARRY2]]) : !cir.bool
// CIR: %{{.*}} = cir.cast bool_to_int %{{.*}} : !cir.bool -> !u32i

// LLVM-LABEL: @test_addc
// LLVM: call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %{{.*}}, i32 %{{.*}})
// LLVM: call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %{{.*}}, i32 %{{.*}})
// LLVM: or i1
// LLVM: zext i1 %{{.*}} to i32

// OGCG-LABEL: @test_addc
// OGCG: call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %{{.*}}, i32 %{{.*}})
// OGCG: call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %{{.*}}, i32 %{{.*}})
// OGCG: or i1
// OGCG: zext i1 %{{.*}} to i32

unsigned int test_subc(unsigned int x, unsigned int y, unsigned int carryin,
                       unsigned int *carryout) {
  return __builtin_subc(x, y, carryin, carryout);
}

// CIR-LABEL: @test_subc
// CIR: %[[SUM1:.*]], %[[CARRY1:.*]] = cir.binop.overflow(sub, %{{.*}}, %{{.*}}) : !u32i, (!u32i, !cir.bool)
// CIR: %[[SUM2:.*]], %[[CARRY2:.*]] = cir.binop.overflow(sub, %[[SUM1]], %{{.*}}) : !u32i, (!u32i, !cir.bool)
// CIR: %{{.*}} = cir.binop(or, %[[CARRY1]], %[[CARRY2]]) : !cir.bool
// CIR: %{{.*}} = cir.cast bool_to_int %{{.*}} : !cir.bool -> !u32i

// LLVM-LABEL: @test_subc
// LLVM: call { i32, i1 } @llvm.usub.with.overflow.i32(i32 %{{.*}}, i32 %{{.*}})
// LLVM: call { i32, i1 } @llvm.usub.with.overflow.i32(i32 %{{.*}}, i32 %{{.*}})
// LLVM: or i1
// LLVM: zext i1 %{{.*}} to i32

// OGCG-LABEL: @test_subc
// OGCG: call { i32, i1 } @llvm.usub.with.overflow.i32(i32 %{{.*}}, i32 %{{.*}})
// OGCG: call { i32, i1 } @llvm.usub.with.overflow.i32(i32 %{{.*}}, i32 %{{.*}})
// OGCG: or i1
// OGCG: zext i1 %{{.*}} to i32

unsigned long long test_addcll(unsigned long long x, unsigned long long y,
                               unsigned long long carryin,
                               unsigned long long *carryout) {
  return __builtin_addcll(x, y, carryin, carryout);
}

// CIR-LABEL: @test_addcll
// CIR: cir.binop.overflow(add, %{{.*}}, %{{.*}}) : !u64i
// CIR: cir.binop.overflow(add, %{{.*}}, %{{.*}}) : !u64i

// LLVM-LABEL: @test_addcll
// LLVM: call { i64, i1 } @llvm.uadd.with.overflow.i64(i64 %{{.*}}, i64 %{{.*}})
// LLVM: call { i64, i1 } @llvm.uadd.with.overflow.i64(i64 %{{.*}}, i64 %{{.*}})

// OGCG-LABEL: @test_addcll
// OGCG: call { i64, i1 } @llvm.uadd.with.overflow.i64(i64 %{{.*}}, i64 %{{.*}})
// OGCG: call { i64, i1 } @llvm.uadd.with.overflow.i64(i64 %{{.*}}, i64 %{{.*}})
