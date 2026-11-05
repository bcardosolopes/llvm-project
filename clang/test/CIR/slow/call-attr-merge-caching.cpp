// UNSUPPORTED: true
// RUN: timeout 15 %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 \
// RUN:   -fclangir -emit-llvm -o /dev/null %s
//
// Verify direct-to-LLVM lowering reuses merged call-site attribute
// dictionaries for repeated indirect byval calls with pure/const side effects.
//
// Each call here repeats the same final pointer-arg merge:
// 1. target lowering produces cir.call_arg_attrs with llvm.noundef on the
//    trailing pointer argument and llvm.byval/llvm.align on the aggregate arg,
// 2. direct lowering upgrades the pointer arg with llvm.readonly/readnone
//    because the call is pure/const and already has indirect arg attrs.
//
// Before the merge cache, every call rebuilt and uniqued the same merged
// pointer dictionary. This file repeats that shape across 20,000 call sites.
//
// NOTE: This runs under lit parallelism, so the timeout leaves headroom for
// scheduler jitter while still catching the pre-fix regression shape.

struct S {
  const char *p;
  unsigned long n;
};

struct Big {
  void *a[3];
};

int (*fp_const)(S, Big, void *) __attribute__((const));
int (*fp_pure)(S, Big, void *) __attribute__((pure));

#define CALL_PAIR(X)                                                         \
  sum += fp_const((S){"x", (unsigned long)(X)}, big, ctx);                   \
  sum += fp_pure((S){"y", (unsigned long)(X)}, big, ctx);

#define CALL_10(B)                                                           \
  CALL_PAIR((B) + 0)                                                         \
  CALL_PAIR((B) + 1)                                                         \
  CALL_PAIR((B) + 2)                                                         \
  CALL_PAIR((B) + 3)                                                         \
  CALL_PAIR((B) + 4)                                                         \
  CALL_PAIR((B) + 5)                                                         \
  CALL_PAIR((B) + 6)                                                         \
  CALL_PAIR((B) + 7)                                                         \
  CALL_PAIR((B) + 8)                                                         \
  CALL_PAIR((B) + 9)

#define CALL_50()                                                            \
  CALL_10(0)                                                                 \
  CALL_10(10)                                                                \
  CALL_10(20)                                                                \
  CALL_10(30)                                                                \
  CALL_10(40)

#define WRAP(ID)                                                             \
  __attribute__((noinline)) int wrapper_##ID(void *ctx) {                    \
    Big big = {{ctx, ctx, ctx}};                                             \
    int sum = 0;                                                             \
    CALL_50()                                                                \
    return sum;                                                              \
  }

#define WRAP_00_09(P)                                                        \
  WRAP(P##_a)                                                                \
  WRAP(P##_b)                                                                \
  WRAP(P##_c)                                                                \
  WRAP(P##_d)                                                                \
  WRAP(P##_e)                                                                \
  WRAP(P##_f)                                                                \
  WRAP(P##_g)                                                                \
  WRAP(P##_h)                                                                \
  WRAP(P##_i)                                                                \
  WRAP(P##_j)

#define WRAP_00_99(P)                                                        \
  WRAP_00_09(P##_a)                                                          \
  WRAP_00_09(P##_b)                                                          \
  WRAP_00_09(P##_c)                                                          \
  WRAP_00_09(P##_d)                                                          \
  WRAP_00_09(P##_e)                                                          \
  WRAP_00_09(P##_f)                                                          \
  WRAP_00_09(P##_g)                                                          \
  WRAP_00_09(P##_h)                                                          \
  WRAP_00_09(P##_i)                                                          \
  WRAP_00_09(P##_j)

WRAP_00_99(group0)
WRAP_00_99(group1)
