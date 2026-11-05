// RUN: %clang_cc1 -triple armv7-none-linux-androideabi -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// On 32-bit ARM, `long double` shares `double`'s 64-bit format. Bionic's
// <stdlib.h> declares `long double strtold(...) __RENAME_LDBL(strtod, ...)`,
// giving the `long double`-returning `strtold` the assembler label `strtod`.
// A single CIR symbol @strtod is then referenced with two return types:
// !cir.double (from strtod) and !cir.long_double<!cir.double> (from strtold).
// Classic CodeGen never sees a conflict because both lower to LLVM `double`.
// CIR must keep one canonical decl type (!cir.double) and coerce the
// long-double call result with a same-size bitcast rather than flipping the
// declaration (which would leave the other call site's result type stale and
// fail CIR verification).

extern "C" double strtod(const char *, char **);
extern "C" long double strtold(const char *, char **) __asm__("strtod");

double call_d(const char *s, char **e) { return strtod(s, e); }

// Feed the long-double result into a conversion/comparison so the call result
// is actually used.
bool call_ld(const char *s, char **e) {
  long double v = strtold(s, e);
  return v > 0.0L;
}

// The plain double call matches the declaration directly.
// CHECK-LABEL: cir.func {{.*}}@_Z6call_dPKcPPc
// CHECK: cir.call @strtod({{.*}}) : (!cir.ptr<!s8i>, !cir.ptr<!cir.ptr<!s8i>>) -> !cir.double

// The declaration must keep the canonical !cir.double return type.
// CHECK: cir.func private @strtod(!cir.ptr<!s8i>{{.*}}, !cir.ptr<!cir.ptr<!s8i>>{{.*}}) -> !cir.double

// The long-double call goes to the same @strtod symbol, returns !cir.double,
// then bitcasts the result to !cir.long_double<!cir.double>.
// CHECK-LABEL: cir.func {{.*}}@_Z7call_ldPKcPPc
// CHECK: %[[R:.*]] = cir.call @strtod({{.*}}) : (!cir.ptr<!s8i>, !cir.ptr<!cir.ptr<!s8i>>) -> !cir.double
// CHECK: cir.cast bitcast %[[R]] : !cir.double -> !cir.long_double<!cir.double>
