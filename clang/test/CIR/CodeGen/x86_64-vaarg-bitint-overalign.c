// RUN: %clang_cc1 -std=c23 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s

// A MEMORY-class aggregate containing a _BitInt(N>64) member must NOT round the
// varargs overflow area up to 16: its x86-64 psABI alignment is 8
// (_Alignof(_BitInt(65)) == 8). CIR models _BitInt(65) as an i128-shaped,
// 16-byte type, so the CIR data-layout alignment (16) over-reports the psABI
// alignment (8). Before the fix, LoweringPrepare took max(dataLayoutAlign=16,
// hint) and CIRGen only attached the psABI hint when it *exceeded* the CIR
// alignment, so the overflow pointer was rounded up to 16 (a spurious
// llvm.ptrmask(..., -16)); when another MEMORY-class arg precedes this one the
// slot is 8-but-not-16 aligned and the _BitInt is read 8 bytes too far. OGCG
// rounds the overflow area up using getTypeAlignInChars (== 8 here), i.e. it
// does NOT round at all. The fix threads the psABI alignment through whenever it
// differs from the CIR alignment (both directions) and uses it directly.

struct Mem24 {
  long a, b, c;
};

struct BI {
  unsigned char c : 4;
  float f __attribute__((aligned(8)));
  _BitInt(65) b;
};

void sink24(struct Mem24 *);
void sinkbi(struct BI *);

void vf(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  struct Mem24 a = __builtin_va_arg(ap, struct Mem24);
  struct BI b = __builtin_va_arg(ap, struct BI);
  sink24(&a);
  sinkbi(&b);
  __builtin_va_end(ap);
}

// The _BitInt(65)-containing aggregate must be read at 8-byte alignment with no
// 16-byte round-up of the overflow area (no llvm.ptrmask with the -16 mask).
// CHECK-LABEL: define dso_local void @vf
// CHECK-NOT: call ptr @llvm.ptrmask.p0.i64(ptr %{{[0-9]+}}, i64 -16)
// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %{{[0-9]+}}, ptr align 8 %{{[0-9]+}}, i64 32, i1 false)
