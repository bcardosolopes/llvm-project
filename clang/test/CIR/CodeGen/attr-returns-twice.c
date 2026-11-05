// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: returns_twice attribute is propagated to function definitions and call
// sites.

__attribute__((returns_twice)) void returns_twice_fn(void) {}

void caller(void) {
    returns_twice_fn();
}

// CIR-LABEL: cir.func {{.*}} @returns_twice_fn()
// CIR-SAME:  extra(#fn_attr
// CIR:       cir.func {{.*}} @caller()
// CIR:       cir.call @returns_twice_fn() {{.*}} extra(#fn_attr

// LLVM: define {{.*}} void @returns_twice_fn() #[[ATTR_RT:[0-9]+]]
// LLVM: define {{.*}} void @caller()
// LLVM:   call void @returns_twice_fn() #[[ATTR_RTCALL:[0-9]+]]
// LLVM: attributes #[[ATTR_RT]] = {{{.*}}returns_twice{{.*}}}
// LLVM: attributes #[[ATTR_RTCALL]] = {{{.*}}returns_twice{{.*}}}

// OGCG: define {{.*}} void @returns_twice_fn() #[[OATTR_RT:[0-9]+]]
// OGCG: define {{.*}} void @caller()
// OGCG:   call void @returns_twice_fn() #[[OATTR_RTCALL:[0-9]+]]
// OGCG: attributes #[[OATTR_RT]] = {{{.*}}returns_twice{{.*}}}
// OGCG: attributes #[[OATTR_RTCALL]] = {{{.*}}returns_twice{{.*}}}
