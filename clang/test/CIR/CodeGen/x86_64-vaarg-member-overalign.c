// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// A MEMORY-class aggregate whose 16-byte alignment comes from a *member*
// __attribute__((aligned(16))) (not a struct-wide attribute) must still round
// the varargs overflow area up to 16 in the callee's va_arg lowering. Before the
// fix CIRGen only attached the cir.x86_overflow_align hint when the AST
// *unadjusted* alignment was exceeded; a member-level aligned(N) already raises
// the unadjusted alignment, so the hint was dropped and the overflow slot was
// read only 8-byte aligned. When another aggregate precedes this one in the same
// variadic call, that leaves the overflow pointer mis-aligned and corrupts the
// fields read here. OGCG rounds the overflow area up using getTypeAlignInChars,
// which the fix reproduces by gating the hint on the CIR data-layout alignment.

struct S1 {
  unsigned long m0;
  unsigned short m1;
  long m2;
} __attribute__((packed));

struct S3 {
  short m0 : 9;
  long m1 __attribute__((aligned(16)));
  struct S1 m2;
};

void sink(struct S3 *);

void vf(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  struct S3 b = __builtin_va_arg(ap, struct S3);
  sink(&b);
  __builtin_va_end(ap);
}

// The overflow area pointer is rounded up to 16 before the aggregate is copied.
// CHECK-LABEL: define dso_local void @vf
// CHECK: call ptr @llvm.ptrmask.p0.i64(ptr %{{[0-9]+}}, i64 -16)
// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 16 %{{[0-9]+}}, ptr align 16 %{{[0-9]+}}, i64 48, i1 false)
