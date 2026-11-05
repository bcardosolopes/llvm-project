// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that large _BitInt local allocas use target-sized storage rather than
// an undersized i129 slot. OGCG spells this as [24 x i8]; direct-CIR may use
// the equivalent widened storage integer type i192.

void take_129(_BitInt(129) x);
_BitInt(129) return_129(void);

void test_bitint_alloca(void) {
  _BitInt(129) x = return_129();
  take_129(x);
}

// LLVM-LABEL: define {{.*}}void @test_bitint_alloca
// LLVM: alloca {{(\[24 x i8\]|i192)}}, {{.*}}align 8

// OGCG-LABEL: define {{.*}}void @test_bitint_alloca
// OGCG: alloca [24 x i8], align 8
