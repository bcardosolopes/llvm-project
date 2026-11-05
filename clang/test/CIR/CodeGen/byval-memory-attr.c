// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-mlir=cir-flat -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR %s < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s < %t.ogcg.ll

// Verify that __attribute__((const)) and __attribute__((pure)) functions with
// byval parameters get memory(argmem: readwrite) instead of memory(none)/
// memory(read). The byval parameter is a caller-allocated copy that the callee
// may read/write, so the memory effect must account for it.

struct S { int A[1000]; };

// __attribute__((const)) with byval: memory(none) -> memory(argmem: readwrite)
int __attribute__((const)) f(struct S x) { x.A[1] = 0; return x.A[0]; }
// CIR-LABEL: cir.func no_inline optnone dso_local @f(
// CIR-SAME: %arg0: !cir.ptr<!rec_S> {cir.align = 8 : i64, cir.byval = !rec_S, cir.noundef}
// CIR: cir.store align(4)
// CIR: cir.load align(8)
// LLVM: define {{.*}} @f(ptr {{.*}} byval(%struct.S) {{.*}}) #[[CONST_ATTR:[0-9]+]]
// LLVM: load i32, ptr %{{.*}}, align 8
// OGCG: define {{.*}} @f(ptr {{.*}} byval(%struct.S) {{.*}}) #[[CONST_ATTR:[0-9]+]]
// OGCG: load i32, ptr %{{.*}}, align 8

// __attribute__((pure)) with byval: memory(read) -> memory(read, argmem: readwrite)
int g(struct S x) __attribute__((pure));
int h(struct S x) { return g(x); }
// LLVM: declare {{.*}} @g(ptr {{.*}} byval(%struct.S) {{.*}}) #[[PURE_ATTR:[0-9]+]]
// OGCG: declare {{.*}} @g(ptr {{.*}} byval(%struct.S) {{.*}}) #[[PURE_ATTR:[0-9]+]]

// LLVM: attributes #[[CONST_ATTR]] = {{{.*}} willreturn memory(argmem: readwrite) {{.*}}}
// OGCG: attributes #[[CONST_ATTR]] = {{{.*}} willreturn memory(argmem: readwrite) {{.*}}}

// LLVM: attributes #[[PURE_ATTR]] = {{{.*}} willreturn memory(read, argmem: readwrite) {{.*}}}
// OGCG: attributes #[[PURE_ATTR]] = {{{.*}} willreturn memory(read, argmem: readwrite) {{.*}}}
