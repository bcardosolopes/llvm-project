// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Optional-like wrappers with nested empty bases and padded payload records
// must still classify as direct INTEGER chunks on x86_64 SysV. The payload is
// a derived wrapper over { union { char; void *; }, bool }, so CIR must not let
// the synthetic padded record body inflate the ABI size and force byval.

struct EmptyPayloadBase {};

struct PayloadBase {
  union Storage {
    char empty;
    void *value;
    constexpr Storage() : empty() {}
    constexpr Storage(void *v) : value(v) {}
  } storage;
  bool engaged;
};

struct Payload : PayloadBase {};

struct EmptyOptionalBase {};
struct OptionalBase : EmptyOptionalBase {
  Payload payload;
};

struct EmptyCopyMove {};
struct Optional : OptionalBase, EmptyCopyMove {};

using Fn = bool (*)(void *, Optional, void *);

// CIR-LABEL: define{{.*}} @_Z4call
// CIR-SAME: (ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr %{{.*}}, i8 %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: byval
// CIR: call{{.*}}i1 %{{.*}}(ptr noundef %{{.*}}, ptr %{{.*}}, i8 %{{.*}}, ptr noundef %{{.*}})
// OGCG-LABEL: define{{.*}} @_Z4call
// OGCG-SAME: (ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr %{{.*}}, i8 %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: byval
// OGCG: call{{.*}}i1 %{{.*}}(ptr noundef %{{.*}}, ptr %{{.*}}, i8 %{{.*}}, ptr noundef %{{.*}})
bool call(Fn fn, void *ctx, Optional opt, void *out) {
  return fn(ctx, opt, out);
}
