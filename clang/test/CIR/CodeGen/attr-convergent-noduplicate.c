// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: convergent and noduplicate attributes are propagated to function
// definitions and call sites.

__attribute__((convergent)) void convergent_fn(void) {}
__attribute__((noduplicate)) void noduplicate_fn(void) {}

void caller(void) {
    convergent_fn();
    noduplicate_fn();
}

// CIR-LABEL: cir.func {{.*}} @convergent_fn()
// CIR-SAME:  extra(#fn_attr
// CIR:       cir.func {{.*}} @noduplicate_fn()
// CIR-SAME:  extra(#fn_attr
// CIR:       cir.func {{.*}} @caller()
// CIR:       cir.call @convergent_fn() {{.*}} extra(#fn_attr
// CIR:       cir.call @noduplicate_fn() {{.*}} extra(#fn_attr

// LLVM: define {{.*}} void @convergent_fn() #[[ATTR_CONV:[0-9]+]]
// LLVM: define {{.*}} void @noduplicate_fn() #[[ATTR_NODUP:[0-9]+]]
// LLVM: define {{.*}} void @caller()
// LLVM:   call void @convergent_fn() #[[ATTR_CONVCALL:[0-9]+]]
// LLVM:   call void @noduplicate_fn() #[[ATTR_NODUPCALL:[0-9]+]]
// LLVM: attributes #[[ATTR_CONV]] = {{{.*}}convergent{{.*}}}
// LLVM: attributes #[[ATTR_NODUP]] = {{{.*}}noduplicate{{.*}}}
// LLVM: attributes #[[ATTR_CONVCALL]] = {{{.*}}convergent{{.*}}}
// LLVM: attributes #[[ATTR_NODUPCALL]] = {{{.*}}noduplicate{{.*}}}

// OGCG: define {{.*}} void @convergent_fn() #[[OATTR_CONV:[0-9]+]]
// OGCG: define {{.*}} void @noduplicate_fn() #[[OATTR_NODUP:[0-9]+]]
// OGCG: define {{.*}} void @caller()
// OGCG:   call void @convergent_fn() #[[OATTR_CONVCALL:[0-9]+]]
// OGCG:   call void @noduplicate_fn() #[[OATTR_NODUPCALL:[0-9]+]]
// OGCG: attributes #[[OATTR_CONV]] = {{{.*}}convergent{{.*}}}
// OGCG: attributes #[[OATTR_NODUP]] = {{{.*}}noduplicate{{.*}}}
// OGCG: attributes #[[OATTR_CONVCALL]] = {{{.*}}convergent{{.*}}}
// OGCG: attributes #[[OATTR_NODUPCALL]] = {{{.*}}noduplicate{{.*}}}
