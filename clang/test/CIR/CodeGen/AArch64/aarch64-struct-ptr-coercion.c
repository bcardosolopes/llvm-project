// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s

// Test that struct-of-pointers are coerced to [N x ptr] instead of [N x i64]
// on AArch64. This preserves alias analysis information through ABI boundaries.

struct OnePtr { void *p; };
struct TwoPtr { void *a; void *b; };
struct Mixed { void *a; int b; };
struct Nested { struct OnePtr a; void *b; };
struct PtrArr { void *arr[2]; };

void use_1(struct OnePtr *);
void use_2(struct TwoPtr *);
void use_mixed(struct Mixed *);
void use_nested(struct Nested *);
void use_ptrarr(struct PtrArr *);

// Single pointer member (8 bytes): coerced to ptr.
// CHECK: define dso_local void @test_one_ptr(ptr %{{.*}})
void test_one_ptr(struct OnePtr s) { use_1(&s); }

// Two pointer members (16 bytes): coerced to [2 x ptr].
// CHECK: define dso_local void @test_two_ptr([2 x ptr] %{{.*}})
void test_two_ptr(struct TwoPtr s) { use_2(&s); }

// Mixed pointer+int members: NOT coerced to [N x ptr], uses [2 x i64].
// CHECK: define dso_local void @test_mixed([2 x i64] %{{.*}})
void test_mixed(struct Mixed s) { use_mixed(&s); }

// Nested struct of pointers: coerced to [2 x ptr].
// CHECK: define dso_local void @test_nested([2 x ptr] %{{.*}})
void test_nested(struct Nested s) { use_nested(&s); }

// Array of pointers: coerced to [2 x ptr].
// CHECK: define dso_local void @test_ptrarr([2 x ptr] %{{.*}})
void test_ptrarr(struct PtrArr s) { use_ptrarr(&s); }

// aligned(16) struct of pointers: uses [2 x ptr] (NOT i128) because AAPCS
// uses the unadjusted (natural) alignment for argument classification, not
// the user-specified alignment from __attribute__((aligned)).
struct __attribute__((aligned(16))) TwoPtrAlign16 { void *a; void *b; };
void use_align16(struct TwoPtrAlign16 *);
// CHECK: define dso_local void @test_two_ptr_align16([2 x ptr] %{{.*}})
void test_two_ptr_align16(struct TwoPtrAlign16 s) { use_align16(&s); }

// Wrapper struct around aligned(16) struct: the wrapper inherits the alignment
// but its unadjusted alignment is also elevated (member propagation), so it
// uses i128.
struct WrapAlign16 { struct TwoPtrAlign16 a; };
void use_wrap_align16(struct WrapAlign16 *);
// CHECK: define dso_local void @test_wrap_align16(i128 %{{.*}})
void test_wrap_align16(struct WrapAlign16 s) { use_wrap_align16(&s); }
