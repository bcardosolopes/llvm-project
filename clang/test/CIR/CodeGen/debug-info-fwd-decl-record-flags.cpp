// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -std=gnu++20 -fclangir \
// RUN:   -emit-llvm -debug-info-kind=constructor %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s \
// RUN:   --implicit-check-not="DIFlagFwdDecl | DIFlagTypePassByValue" \
// RUN:   --implicit-check-not="DIFlagFwdDecl | DIFlagTypePassByReference"
// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -std=gnu++20 \
// RUN:   -emit-llvm -debug-info-kind=constructor %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s \
// RUN:   --implicit-check-not="DIFlagFwdDecl | DIFlagTypePassByValue" \
// RUN:   --implicit-check-not="DIFlagFwdDecl | DIFlagTypePassByReference"

// Under limited (-debug-info-kind=constructor) debug info a record whose
// definition is visible in this translation unit is still emitted as a forward
// declaration when nothing requires the complete type. Here Trivial and
// NonTrivial are only reachable as the parameter type of a function-pointer
// typedef used by a module-level global.
//
// Classic CodeGen builds such a node in CGDebugInfo::getOrCreateRecordFwdDecl,
// which sets DIFlagFwdDecl plus, for a C++ record with no definition or a
// non-trivial one, DIFlagNonTrivial. The complete-type flags -- the calling
// convention (DIFlagTypePassByValue / DIFlagTypePassByReference),
// DW_AT_export_symbols and the access specifier -- belong to the complete type
// and are never set on a forward declaration.
//
// The CIR global record-type rewrite used to recompute the complete-type flags
// for these nodes because the definition was visible, emitting the impossible
// combinations "DIFlagFwdDecl | DIFlagTypePassByValue" (Trivial) and
// "DIFlagFwdDecl | DIFlagTypePassByReference" (NonTrivial). The
// --implicit-check-not arguments above pin both down.

struct Trivial {
  int a;
  int b;
};

struct NonTrivial {
  NonTrivial();
  ~NonTrivial();
  int a;
};

struct Complete {
  int a;
};

typedef void (*TrivialFn)(Trivial);
typedef void (*NonTrivialFn)(NonTrivial);

static TrivialFn trivialFn;
static NonTrivialFn nonTrivialFn;

// A global of record type does require the complete type, so this one keeps the
// complete-type flags: the fix must not strip those.
Complete completeGlobal;

TrivialFn getTrivialFn() { return trivialFn; }
NonTrivialFn getNonTrivialFn() { return nonTrivialFn; }

// A trivial record reached only as a forward declaration carries DIFlagFwdDecl
// alone -- no calling-convention flag.
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Trivial", {{.*}}size: 64, flags: DIFlagFwdDecl, identifier: "_ZTS7Trivial")
// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Trivial", {{.*}}size: 64, flags: DIFlagFwdDecl, identifier: "_ZTS7Trivial")

// A non-trivial one adds DIFlagNonTrivial, and still no calling-convention
// flag.
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "NonTrivial", {{.*}}size: 32, flags: DIFlagFwdDecl | DIFlagNonTrivial, identifier: "_ZTS10NonTrivial")
// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "NonTrivial", {{.*}}size: 32, flags: DIFlagFwdDecl | DIFlagNonTrivial, identifier: "_ZTS10NonTrivial")

// The complete record keeps DIFlagTypePassByValue and its members.
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Complete", {{.*}}size: 32, flags: DIFlagTypePassByValue, elements: !{{[0-9]+}}, identifier: "_ZTS8Complete")
// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Complete", {{.*}}size: 32, flags: DIFlagTypePassByValue, elements: !{{[0-9]+}}, identifier: "_ZTS8Complete")
