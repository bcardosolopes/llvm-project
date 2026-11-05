// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
//
// Synthetic stand-in for the mlir::InferTypeOpInterface wrapper mix that
// used to crash clangir-built mlir-irdl-to-cpp on x86_64 SysV. The
// optional-like wrapper, range pair wrappers, pointer wrappers, and trailing
// byval RegionRange must match OGCG on both the function ABI and the indirect
// call site.

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
struct OptionalLocation : OptionalBase, EmptyCopyMove {};

struct ValueRange {
  void *owner;
  long long count;
};

struct DictionaryAttr {
  void *impl;
};

struct OpaqueProperties {
  void *properties;
};

struct RegionRange {
  void *owner;
  long long count;
};

using Fn = bool (*)(void *, OptionalLocation, ValueRange, DictionaryAttr,
                    OpaqueProperties, RegionRange, void *);

// CIR-LABEL: define{{.*}} @_Z4call
// CIR-SAME: (ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr %{{.*}}, i8 %{{.*}}, ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, ptr noundef byval(%struct.RegionRange) align 8 %{{.*}}, ptr noundef %{{.*}})
// CIR: call{{.*}}i1 %{{.*}}(ptr noundef %{{.*}}, ptr %{{.*}}, i8 %{{.*}}, ptr %{{.*}}, i64 %{{.*}}, ptr %{{.*}}, i64 %{{.*}}, ptr noundef byval(%struct.RegionRange) align 8 %{{.*}}, ptr noundef %{{.*}})
//
// OGCG-LABEL: define{{.*}} @_Z4call
// OGCG-SAME: (ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr %{{.*}}, i8 %{{.*}}, ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, ptr noundef byval(%struct.RegionRange) align 8 %{{.*}}, ptr noundef %{{.*}})
// OGCG: call{{.*}}i1 %{{.*}}(ptr noundef %{{.*}}, ptr %{{.*}}, i8 %{{.*}}, ptr %{{.*}}, i64 %{{.*}}, ptr %{{.*}}, i64 %{{.*}}, ptr noundef byval(%struct.RegionRange) align 8 %{{.*}}, ptr noundef %{{.*}})
bool call(Fn fn, void *ctx, OptionalLocation loc, ValueRange operands,
          DictionaryAttr attrs, OpaqueProperties props, RegionRange regions,
          void *out) {
  return fn(ctx, loc, operands, attrs, props, regions, out);
}
