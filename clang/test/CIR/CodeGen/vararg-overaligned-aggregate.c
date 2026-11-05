// Verify that x86-family va_arg rounds the varargs overflow area up to an
// over-aligned aggregate's ABI alignment before fetching it, matching classic
// clang. A struct-wide __attribute__((aligned(16))) raises the type's ABI
// alignment above its members' natural (8-byte) alignment; the CIR record type
// cannot carry that, so CIRGen threads it through for the overflow round-up.
// Running the same CHECK lines over -fclangir and OGCG proves they agree.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=X64 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=X64 %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=X86 %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=X86 %s

typedef __builtin_va_list va_list;

struct Over { long long a, b, c; } __attribute__((aligned(16)));
struct Plain { long long a, b, c; };

// An over-aligned memory-class aggregate: the x86-64 SysV overflow area pointer
// must be rounded up to 16 bytes before the fetch (AMD64-ABI 3.5.7p5 step 7).
long long over(va_list ap) { return __builtin_va_arg(ap, struct Over).a; }

// X64-LABEL: @over(
// X64: getelementptr inbounds{{( nuw)?}} i8, ptr %{{.*}}, i{{32|64}} 15
// X64: call{{.*}} ptr @llvm.ptrmask.p0.i64(ptr {{(nonnull )?}}%{{.*}}, i64 -16)
// X64: call void @llvm.memcpy.p0.p0.i64(ptr align 16 %{{.*}}, ptr align 16 %{{.*}}, i64 32, i1 false)

// On i386 (System V) non-SIMD aggregates keep the 4-byte stack slot alignment;
// the overflow pointer is never rounded up, matching classic clang.
// X86-LABEL: @over(
// X86-NOT: ptrmask

// A plain 8-aligned aggregate must NOT trigger the >8 overflow round-up.
long long plain(va_list ap) { return __builtin_va_arg(ap, struct Plain).a; }

// X64-LABEL: @plain(
// X64-NOT: call{{.*}}ptrmask
