// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ftrapv -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ftrapv -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

unsigned int ui, uj, uk;
int i, j, k;

// CIR-LABEL: @test_unsigned_add
// LLVM-LABEL: @test_unsigned_add
void test_unsigned_add(void) {
  // -ftrapv doesn't affect unsigned arithmetic.
  // CIR: cir.binop(add,
  // CIR-NOT: cir.binop.overflow
  // LLVM: add i32
  // LLVM-NOT: @llvm.sadd.with.overflow
  ui = uj + uk;
}

// CIR-LABEL: @test_signed_add
// LLVM-LABEL: @test_signed_add
void test_signed_add(void) {
  // CIR: cir.binop.overflow(add,
  // CIR-SAME: {cir.nosanitize}
  // CIR: cir.if
  // CIR: cir.trap
  // CIR-SAME: {cir.nosanitize, cir.ubsantrap_handler = 0 : i8}
  // LLVM: call { i32, i1 } @llvm.sadd.with.overflow.i32
  // LLVM: extractvalue { i32, i1 }
  // LLVM-SAME: !nosanitize ![[NOSAN:[0-9]+]]
  // LLVM: extractvalue { i32, i1 }
  // LLVM-SAME: !nosanitize ![[NOSAN]]
  // LLVM: br i1
  // LLVM-SAME: !nosanitize ![[NOSAN]]
  // LLVM: call void @llvm.ubsantrap(i8 0)
  // LLVM-SAME: !nosanitize ![[NOSAN]]
  // LLVM: unreachable
  // LLVM-SAME: !nosanitize ![[NOSAN]]
  i = j + k;
}

// CIR-LABEL: @test_signed_sub
// LLVM-LABEL: @test_signed_sub
void test_signed_sub(void) {
  // CIR: cir.binop.overflow(sub,
  // CIR: cir.if
  // CIR: cir.trap
  // LLVM: call { i32, i1 } @llvm.ssub.with.overflow.i32
  // LLVM: call void @llvm.ubsantrap(i8 21)
  i = j - k;
}

// CIR-LABEL: @test_signed_mul
// LLVM-LABEL: @test_signed_mul
void test_signed_mul(void) {
  // CIR: cir.binop.overflow(mul,
  // CIR: cir.if
  // CIR: cir.trap
  // LLVM: call { i32, i1 } @llvm.smul.with.overflow.i32
  // LLVM: call void @llvm.ubsantrap(i8 12)
  i = j * k;
}

// CIR-LABEL: @test_postinc
// LLVM-LABEL: @test_postinc
void test_postinc(void) {
  // CIR: cir.binop.overflow(add,
  // CIR: cir.trap
  // LLVM: call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %{{.*}}, i32 1)
  // LLVM: call void @llvm.ubsantrap(i8 0)
  i++;
}

// CIR-LABEL: @test_preinc
// LLVM-LABEL: @test_preinc
void test_preinc(void) {
  // CIR: cir.binop.overflow(add,
  // CIR: cir.trap
  // LLVM: call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %{{.*}}, i32 1)
  // LLVM: call void @llvm.ubsantrap(i8 0)
  ++i;
}

// CIR-LABEL: @test_postdec
// LLVM-LABEL: @test_postdec
void test_postdec(void) {
  // CIR: cir.binop.overflow(sub,
  // CIR: cir.trap
  // LLVM: call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 %{{.*}}, i32 1)
  // LLVM: call void @llvm.ubsantrap(i8 21)
  i--;
}

// CIR-LABEL: @test_predec
// LLVM-LABEL: @test_predec
void test_predec(void) {
  // CIR: cir.binop.overflow(sub,
  // CIR: cir.trap
  // LLVM: call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 %{{.*}}, i32 1)
  // LLVM: call void @llvm.ubsantrap(i8 21)
  --i;
}

// CIR-LABEL: @test_negate
// LLVM-LABEL: @test_negate
void test_negate(void) {
  // Negation under -ftrapv is synthesized as 0 - val.
  // CIR: cir.binop.overflow(sub,
  // CIR: cir.trap
  // LLVM: call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 0, i32 %{{.*}})
  // LLVM: call void @llvm.ubsantrap(i8 21)
  i = -j;
}

// CIR-LABEL: @test_no_trap_div_rem
// LLVM-LABEL: @test_no_trap_div_rem
void test_no_trap_div_rem(int a, int b, float c, float d) {
  // Division, remainder, and float division should NOT trigger overflow traps.
  // CIR-NOT: cir.binop.overflow
  // CIR-NOT: cir.trap
  // LLVM-NOT: @llvm.sadd.with.overflow
  // LLVM-NOT: call void @llvm.ubsantrap
  (void)(a / b);
  (void)(a % b);
  (void)(c / d);
  // CIR: cir.return
  // LLVM: ret void
}

// CIR-LABEL: @test_builtin_abs
// LLVM-LABEL: @test_builtin_abs
int test_builtin_abs(int x) {
  // __builtin_abs with -ftrapv checks for INT_MIN overflow on negation.
  // CIR: cir.binop.overflow(sub,
  // CIR: cir.trap
  // CIR: cir.abs %{{.*}} poison
  // LLVM: call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 0, i32 %{{.*}})
  // LLVM: call void @llvm.ubsantrap(i8 21)
  // LLVM: call i32 @llvm.abs.i32(i32 %{{.*}}, i1 true)
  return __builtin_abs(x);
}
