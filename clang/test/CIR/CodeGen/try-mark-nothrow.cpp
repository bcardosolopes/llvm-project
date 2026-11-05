// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that functions whose bodies contain no throwing operations are
// opportunistically marked nounwind, matching OGCG's TryMarkNoThrow.

void may_throw();

// Trivially nothrow — no calls at all. Uses same attribute group as noexcept_fn.
// CHECK: define {{.*}} @_Z5plaini({{.*}}) #[[NW:[0-9]+]]
// OGCG: define {{.*}} @_Z5plaini({{.*}}) #[[NW:[0-9]+]]
int plain(int x) { return x + 1; }

// Calls an extern that might throw — different attribute group.
// CHECK: define {{.*}} @_Z11calls_throwv() #[[MT:[0-9]+]]
// OGCG: define {{.*}} @_Z11calls_throwv() #[[MT:[0-9]+]]
void calls_throw() { may_throw(); }

// Explicitly noexcept — same nounwind group as plain.
// CHECK: define {{.*}} @_Z11noexcept_fnv() #[[NW]]
// OGCG: define {{.*}} @_Z11noexcept_fnv() #[[NW]]
void noexcept_fn() noexcept {}

// Has a throw statement — same may-throw group as calls_throw.
// CHECK: define {{.*}} @_Z9throws_fnv() #[[MT]]
// OGCG: define {{.*}} @_Z9throws_fnv() #[[MT]]
void throws_fn() { throw 42; }

// Template instantiation (LinkOnceODR linkage) — should still get nounwind
// since ODR guarantees no replacement. Tests that isInterposableLinkage
// is used instead of isWeakForLinker.
template<typename T>
T tmpl_add(T a, T b) { return a + b; }
// CHECK: define {{.*}} @_Z8tmpl_addIiET_S0_S0_({{.*}}) #[[NW]]
// OGCG: define {{.*}} @_Z8tmpl_addIiET_S0_S0_({{.*}}) #[[NW]]
int use_tmpl() { return tmpl_add(1, 2); }

// CHECK: attributes #[[NW]] = {{.*}}nounwind{{.*}}
// OGCG: attributes #[[NW]] = {{.*}}nounwind{{.*}}
