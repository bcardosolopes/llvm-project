// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR --check-prefix=COMMON --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --check-prefix=COMMON --input-file=%t.ogcg.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj %s -o /dev/null
//
// va_arg with struct that uses mixed GP + SSE registers on x86-64.

typedef struct { int a; double b; } Mixed;

typedef __builtin_va_list va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

double sum_mixed(int n, ...) {
  va_list ap;
  va_start(ap, n);
  double total = 0;
  for (int i = 0; i < n; i++) {
    Mixed m = va_arg(ap, Mixed);
    total += m.a + m.b;
  }
  va_end(ap);
  return total;
}

// Mixed GP+SSE struct: check both gp_offset and fp_offset thresholds.
// GP threshold: 48 - 1*8 = 40, FP threshold: 176 - 1*16 = 160.
// COMMON-LABEL: define {{.*}} @sum_mixed(
// COMMON: icmp ule i32 %{{.+}}, 40
// COMMON: icmp ule i32 %{{.+}}, 160
// COMMON: and i1
// COMMON: br i1
// CIR: %{{.*}} = getelementptr inbounds nuw { i32, double }, ptr %{{.*}}, i32 0, i32 1
// CIR-NEXT: store double %{{.*}}, ptr %{{.*}}, align 8
// OGCG: %{{.*}} = getelementptr inbounds nuw { i32, double }, ptr %{{.*}}, i32 0, i32 1
// OGCG-NEXT: store double %{{.*}}, ptr %{{.*}}, align 8
