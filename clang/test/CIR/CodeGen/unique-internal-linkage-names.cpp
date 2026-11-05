// This test checks if internal linkage symbols get unique names with
// -funique-internal-linkage-names option.
// RUN: %clang_cc1 -triple x86_64-linux-gnu -x c++ -fclangir -emit-llvm \
// RUN:     -funique-internal-linkage-names %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=UNIQUE
// RUN: %clang_cc1 -triple x86_64-linux-gnu -x c++ -fclangir -emit-llvm \
// RUN:     %s -o %t.plain.ll
// RUN: FileCheck --input-file=%t.plain.ll %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -x c++ -emit-llvm \
// RUN:     -funique-internal-linkage-names %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

static int glob;
static int foo() {
  return 0;
}

int (*bar())() {
  return foo;
}

int getGlob() {
  return glob;
}

// Function local static variable and anonymous namespace variable.
namespace {
int anon_m;
int getM() {
  return anon_m;
}
} // namespace

int retAnonM() {
  static int fGlob;
  return getM() + fGlob;
}

namespace {
class A {
public:
  A() {}
  ~A() {}
};
}

void test() {
  A a;
}

// Internal-linkage global variables SHOULD also get the unique suffix
// (matches OG behavior in 22.x).
// UNIQUE-DAG: @_ZL4glob = internal global
// UNIQUE-DAG: @_ZZ8retAnonMvE5fGlob = internal global
// UNIQUE-DAG: @_ZN12_GLOBAL__N_16anon_mE = internal global

// Internal linkage functions SHOULD get the unique suffix.
// UNIQUE: define internal noundef i32 @_ZL3foov.[[MODHASH:__uniq\.[0-9]+]]() #[[#ATTR:]]
// UNIQUE: define internal noundef i32 @_ZN12_GLOBAL__N_14getMEv.[[MODHASH]]
// UNIQUE: define internal void @_ZN12_GLOBAL__N_11AC1Ev.[[MODHASH]]
// UNIQUE: define internal void @_ZN12_GLOBAL__N_11AD1Ev.[[MODHASH]]
// UNIQUE: attributes #[[#ATTR]] = { {{.*}}"sample-profile-suffix-elision-policy"{{.*}} }

// Without -funique-internal-linkage-names, no suffix should be appended.
// PLAIN: define internal noundef i32 @_ZL3foov()
// PLAIN: define internal noundef i32 @_ZN12_GLOBAL__N_14getMEv
// PLAIN-NOT: "sample-profile-suffix-elision-policy"

// OGCG cross-check: same behavior as CIR.
// OGCG-DAG: @_ZL4glob = internal global
// OGCG-DAG: @_ZZ8retAnonMvE5fGlob = internal global
// OGCG-DAG: @_ZN12_GLOBAL__N_16anon_mE = internal global
// OGCG: define internal noundef i32 @_ZL3foov.[[OGCG_HASH:__uniq\.[0-9]+]]() #[[#OGCG_ATTR:]]
// OGCG: define internal noundef i32 @_ZN12_GLOBAL__N_14getMEv.[[OGCG_HASH]]
// OGCG: define internal void @_ZN12_GLOBAL__N_11AC1Ev.[[OGCG_HASH]]
// OGCG: define internal void @_ZN12_GLOBAL__N_11AD1Ev.[[OGCG_HASH]]
// OGCG: attributes #[[#OGCG_ATTR]] = { {{.*}}"sample-profile-suffix-elision-policy"{{.*}} }
