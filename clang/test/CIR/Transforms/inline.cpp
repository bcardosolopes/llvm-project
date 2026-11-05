// The CIR inliner (-fclangir-inline): small callees and only uses are
// inlined; what v1 refuses stays a call. LLVM's own passes are off, so every
// inline below is CIR's.
//
// RUN: %clang_cc1 -triple aarch64-linux-android28 -std=c++17 -fclangir -O2 \
// RUN:     -fclangir-mem2reg -fclangir-inline -disable-llvm-passes \
// RUN:     -fcxx-exceptions -fexceptions -fvisibility-inlines-hidden \
// RUN:     -emit-llvm %s -o - | FileCheck %s
//
// With constructor aliases (the driver's default), the alias survives.
// RUN: %clang_cc1 -triple aarch64-linux-android28 -std=c++17 -fclangir -O2 \
// RUN:     -fclangir-mem2reg -fclangir-inline -disable-llvm-passes \
// RUN:     -mconstructor-aliases -fcxx-exceptions -fexceptions \
// RUN:     -emit-llvm %s -o - | FileCheck %s --check-prefix=ALIAS
// ALIAS: @_ZN3OutC1Ev = {{.*}}alias {{.*}}@_ZN3OutC2Ev
//
// -fclangir-inline-threshold raises the size limit.
// RUN: %clang_cc1 -triple aarch64-linux-android28 -std=c++17 -fclangir -O2 \
// RUN:     -fclangir-mem2reg -fclangir-inline -fclangir-inline-threshold=100 \
// RUN:     -disable-llvm-passes -fcxx-exceptions -fexceptions \
// RUN:     -emit-llvm %s -o - | FileCheck %s --check-prefix=BIG
//
// Without the flag nothing is inlined.
// RUN: %clang_cc1 -triple aarch64-linux-android28 -std=c++17 -fclangir -O2 \
// RUN:     -fclangir-mem2reg -disable-llvm-passes -fcxx-exceptions -fexceptions \
// RUN:     -fvisibility-inlines-hidden -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OFF

// The global constructor at the end of the file is named only from the
// module's constructor list, so nothing calls it; it must survive inlining.
// CHECK: @llvm.global_ctors = {{.*}}@_GLOBAL__sub_I_

int work(int);
int vwork(int, ...);

struct Vec {
  int *data;
  int n;
  int size() const { return n; }
  int &operator[](int i) { return data[i]; }
};

// Getters, operator[] and a forwarding wrapper are inlined, and the hidden
// linkonce_odr copies left without a use are dropped.
static int forward(int x) { return work(x); }
int sum(Vec &v) {
  int s = 0;
  for (int i = 0; i < v.size(); ++i)
    s += v[i] + forward(i);
  return s;
}
// CHECK-LABEL: define {{.*}} @_Z3sumR3Vec(
// CHECK-NOT:     call {{.*}}@_ZNK3Vec4sizeEv
// CHECK-NOT:     call {{.*}}@_ZN3VecixEi
// CHECK-NOT:     call {{.*}}@_ZL7forwardi
// CHECK:         call {{.*}}@_Z4worki(
// CHECK:       }
// OFF-LABEL:   define {{.*}} @_Z3sumR3Vec(
// OFF:           call {{.*}}@_ZNK3Vec4sizeEv
// OFF:           call {{.*}}@_ZN3VecixEi
// OFF:           call {{.*}}@_ZL7forwardi

// A callee too big for the threshold is inlined anyway when the call is its
// only use, and then dropped.
static int once(int a, int b) {
  int s = 0;
  for (int i = 0; i < a; ++i)
    s += work(i * b) + work(i + b) + work(i - b);
  return s;
}
int callsOnce(int a) { return once(a, 3); }
// CHECK-LABEL: define {{.*}} @_Z9callsOncei(
// CHECK-NOT:     call {{.*}}@_ZL4onceii
// CHECK:       }

// Too big, and used twice: stays a call.
int big(int a) {
  int s = 0;
  for (int i = 0; i < a; ++i)
    s += work(i) + work(i * 2) + work(i * 3);
  return s;
}
int callsBigTwice(int a) { return big(a) + big(a + 1); }
// CHECK-LABEL: define {{.*}} @_Z13callsBigTwicei(
// CHECK:         call {{.*}}@_Z3bigi(
// CHECK:         call {{.*}}@_Z3bigi(
// BIG-LABEL: define {{.*}} @_Z13callsBigTwicei(
// BIG-NOT:     call {{.*}}@_Z3bigi(
// BIG:       }

// An annotated local inlined from its only caller keeps the annotation, with
// the line of the variable in the callee.
void use(int *);
static void annotated() {
  int x __attribute__((annotate("tag")));
  use(&x);
}
void callsAnnotated() { annotated(); }
// CHECK-LABEL: define {{.*}} @_Z14callsAnnotatedv(
// CHECK:         call void @llvm.var.annotation.p0.p0(ptr {{.*}}, ptr {{.*}}, ptr {{.*}}, i32 [[@LINE-5]],
// CHECK-NOT:     call {{.*}}@_ZL9annotatedv

// A musttail call has to stay right before its function's return: a callee
// holding one is not inlined.
int target(int);
static int tailer(int x) { [[clang::musttail]] return target(x); }
int callsTailer(int x) { return tailer(x) + 1; }
// CHECK-LABEL: define {{.*}} @_Z11callsTaileri(
// CHECK:         call {{.*}}@_ZL6taileri(

// noinline on the callee or on the call site is honoured.
__attribute__((noinline)) int tinyNoInline(int x) { return x + 1; }
int tiny(int x) { return x + 2; }
int callsNoInline(int x) {
  int r = tinyNoInline(x);
  [[clang::noinline]] r += tiny(x);
  return r;
}
// CHECK-LABEL: define {{.*}} @_Z13callsNoInlinei(
// CHECK:         call {{.*}}@_Z12tinyNoInlinei(
// CHECK:         call {{.*}}@_Z4tinyi(

// v1 refuses a call site inside a try (an invoke) and a callee with EH.
int mayThrow(int x) { return work(x); }
int callsInTry(int x) {
  try {
    return mayThrow(x);
  } catch (...) {
    return -1;
  }
}
// CHECK-LABEL: define {{.*}} @_Z10callsInTryi(
// CHECK:         invoke {{.*}}@_Z8mayThrowi(
int catches(int x) {
  try {
    return work(x);
  } catch (...) {
    return 0;
  }
}
int callsCatches(int x) { return catches(x) + catches(x + 1); }
// CHECK-LABEL: define {{.*}} @_Z12callsCatchesi(
// CHECK:         call {{.*}}@_Z7catchesi(

// Nor a varargs callee.
int varargsCallee(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  int r = __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  return r;
}
int callsVarargs(int x) { return varargsCallee(1, x) + varargsCallee(1, x); }
// CHECK-LABEL: define {{.*}} @_Z12callsVarargsi(
// CHECK:         call {{.*}}@_Z13varargsCalleeiz(

// A callee's local that mem2reg cannot promote moves to the caller's entry.
struct Pair {
  int a, b;
};
int usePair(Pair *);
static int withLocal(int x) {
  Pair p{x, x + 1};
  return usePair(&p);
}
int callsWithLocal(int x, int y) {
  if (x > y)
    return withLocal(x);
  return 0;
}
// CHECK-LABEL: define {{.*}} @_Z14callsWithLocalii(
// CHECK-NEXT:    alloca %struct.Pair
// CHECK-NOT:     call {{.*}}@_ZL9withLocali
// CHECK:         call {{.*}}@_Z7usePairP4Pair(

// The hidden linkonce_odr getters were dropped once inlined everywhere.
// CHECK-NOT: define {{.*}} @_ZNK3Vec4sizeEv(
// CHECK-NOT: define {{.*}} @_ZN3VecixEi(

// The global constructor (see the top of the file); its inits are inlined into
// it.
struct Counter {
  int n;
  Counter() : n(work(0)) {}
};
Counter counter;

// An indirect call does not trip the call graph; its target is never inlined.
int callsIndirect(int (*fp)(int), int x) { return fp(x) + tiny(x); }
// CHECK-LABEL: define {{.*}} @_Z13callsIndirectPFiiEi(
// CHECK:         call {{.*}} %0(
// CHECK-NOT:     call {{.*}}@_Z4tinyi(
// CHECK:       }
// CHECK: define internal void @_GLOBAL__sub_I_

// An out-of-line constructor's complete-object variant is an alias of the
// base-object one; aliases have no body to inline and must survive, as the
// other TUs that call them link against them (ALIAS, at the top).
struct Out {
  Out();
  int v;
};
Out::Out() : v(work(1)) {}
