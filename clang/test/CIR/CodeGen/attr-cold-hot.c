// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: cold/hot attributes are propagated to function definitions and call
// sites.

__attribute__((cold)) void cold_fn(void) {}
__attribute__((hot)) void hot_fn(void) {}

void caller(void) {
    cold_fn();
    hot_fn();
}

// CIR-LABEL: cir.func {{.*}} @cold_fn()
// CIR-SAME:  extra(#fn_attr
// CIR:       cir.func {{.*}} @hot_fn()
// CIR-SAME:  extra(#fn_attr
// CIR:       cir.func {{.*}} @caller()
// CIR:       cir.call @cold_fn() {{.*}} extra(#fn_attr
// CIR:       cir.call @hot_fn() {{.*}} extra(#fn_attr

// LLVM: define {{.*}} void @cold_fn() #[[ATTR_COLD:[0-9]+]]
// LLVM: define {{.*}} void @hot_fn() #[[ATTR_HOT:[0-9]+]]
// LLVM: define {{.*}} void @caller()
// LLVM:   call void @cold_fn() #[[ATTR_COLDCALL:[0-9]+]]
// LLVM:   call void @hot_fn() #[[ATTR_HOTCALL:[0-9]+]]
// LLVM: attributes #[[ATTR_COLD]] = {{{.*}}cold{{.*}}}
// LLVM: attributes #[[ATTR_HOT]] = {{{.*}}hot{{.*}}}
// LLVM: attributes #[[ATTR_COLDCALL]] = {{{.*}}cold{{.*}}}
// LLVM: attributes #[[ATTR_HOTCALL]] = {{{.*}}hot{{.*}}}

// OGCG: define {{.*}} void @cold_fn() #[[OATTR_COLD:[0-9]+]]
// OGCG: define {{.*}} void @hot_fn() #[[OATTR_HOT:[0-9]+]]
// OGCG: define {{.*}} void @caller()
// OGCG:   call void @cold_fn() #[[OATTR_COLDCALL:[0-9]+]]
// OGCG:   call void @hot_fn() #[[OATTR_HOTCALL:[0-9]+]]
// OGCG: attributes #[[OATTR_COLD]] = {{{.*}}cold{{.*}}}
// OGCG: attributes #[[OATTR_HOT]] = {{{.*}}hot{{.*}}}
// OGCG: attributes #[[OATTR_COLDCALL]] = {{{.*}}cold{{.*}}}
// OGCG: attributes #[[OATTR_HOTCALL]] = {{{.*}}hot{{.*}}}
