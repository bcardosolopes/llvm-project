// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test __builtin_cpu_init, __builtin_cpu_is, and __builtin_cpu_supports.

extern void a(const char *);

// CIR: cir.global "private" external dso_local @__cpu_model
// LLVM: @__cpu_model = external dso_local global { i32, i32, i32, [1 x i32] }

void test_cpu_init(void) {
  __builtin_cpu_init();
}

// CIR: cir.func {{.*}} @test_cpu_init
// CIR:   cir.call @__cpu_indicator_init() : () -> ()

// LLVM: define {{.*}} @test_cpu_init
// LLVM:   call void @__cpu_indicator_init()

void test_cpu_is_intel(void) {
  if (__builtin_cpu_is("intel"))
    a("intel");
}

// intel is a vendor (field index 0, value 1).
// CIR: cir.func {{.*}} @test_cpu_is_intel
// CIR:   cir.get_global @__cpu_model
// CIR:   cir.get_member
// CIR:   cir.load align(4)
// CIR:   cir.const #cir.int<1>
// CIR:   cir.cmp(eq,

// LLVM: define {{.*}} @test_cpu_is_intel
// LLVM:   %[[VAL:.*]] = load i32, ptr @__cpu_model, align 4
// LLVM:   icmp eq i32 %[[VAL]], 1

void test_cpu_is_atom(void) {
  if (__builtin_cpu_is("atom"))
    a("atom");
}

// atom is a CPU type (field index 1).
// CIR: cir.func {{.*}} @test_cpu_is_atom
// CIR:   cir.get_global @__cpu_model
// CIR:   cir.get_member
// CIR:   cir.load align(4)
// CIR:   cir.cmp(eq,

// LLVM: define {{.*}} @test_cpu_is_atom
// LLVM:   load i32, ptr getelementptr inbounds{{.*}}(i8, ptr @__cpu_model, i64 4)
// LLVM:   icmp eq i32 %{{.*}}, 1

void test_cpu_is_nehalem(void) {
  if (__builtin_cpu_is("nehalem"))
    a("nehalem");
}

// nehalem is a CPU subtype (field index 2).
// CIR: cir.func {{.*}} @test_cpu_is_nehalem
// CIR:   cir.get_global @__cpu_model
// CIR:   cir.get_member
// CIR:   cir.load align(4)
// CIR:   cir.cmp(eq,

// LLVM: define {{.*}} @test_cpu_is_nehalem
// LLVM:   load i32, ptr getelementptr inbounds{{.*}}(i8, ptr @__cpu_model, i64 8)

void test_cpu_supports_sse42(void) {
  if (__builtin_cpu_supports("sse4.2"))
    a("sse4.2");
}

// sse4.2 has a non-zero mask[0], so it uses __cpu_model[3].
// CIR: cir.func {{.*}} @test_cpu_supports_sse42
// CIR:   cir.const #true
// CIR:   cir.get_global @__cpu_model
// CIR:   cir.get_member
// CIR:   cir.const #cir.int<0>
// CIR:   cir.get_element
// CIR:   cir.load align(4)
// CIR:   cir.const #cir.int<256>
// CIR:   cir.binop(and,
// CIR:   cir.cmp(eq,
// CIR:   cir.binop(and,

// LLVM: define {{.*}} @test_cpu_supports_sse42
// LLVM:   %[[FEAT:.*]] = load i32, ptr getelementptr inbounds{{.*}}(i8, ptr @__cpu_model, i64 12)
// LLVM:   %[[AND:.*]] = and i32 %[[FEAT]], 256
// LLVM:   %[[CMP:.*]] = icmp eq i32 %[[AND]], 256
// LLVM:   and i1 true, %[[CMP]]

void test_cpu_supports_gfni(void) {
  if (__builtin_cpu_supports("gfni"))
    a("gfni");
}

// gfni has a non-zero mask[1], so it uses __cpu_features2[0].
// CIR: cir.func {{.*}} @test_cpu_supports_gfni
// CIR:   cir.get_global @__cpu_features2
// CIR:   cir.const #cir.int<0>
// CIR:   cir.get_element
// CIR:   cir.load
// CIR:   cir.binop(and,
// CIR:   cir.cmp(eq,

// LLVM: define {{.*}} @test_cpu_supports_gfni
// LLVM:   %[[FEAT:.*]] = load i32, ptr @__cpu_features2, align 4
// LLVM:   %[[AND:.*]] = and i32 %[[FEAT]], 1
// LLVM:   %[[CMP:.*]] = icmp eq i32 %[[AND]], 1
// LLVM:   and i1 true, %[[CMP]]
