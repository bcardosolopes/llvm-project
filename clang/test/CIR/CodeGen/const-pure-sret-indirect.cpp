// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.og.ll %s

// Ensure indirect calls to const/pure functions returning via sret upgrade
// memory effects to include argmem writes. Without this, LLVM rejects
// writable+sret combined with memory(none)/memory(read).

struct S {
  int x;
  ~S();
};

S (*fp_const)() __attribute__((const));
S (*fp_pure)() __attribute__((pure));

S call_const() { return fp_const(); }
S call_pure() { return fp_pure(); }

// CIR-LABEL: define dso_local void @_Z10call_constv(
// CIR: call void %{{.+}}(ptr dead_on_unwind writable sret(%struct.S) align 4 %{{.+}}) #[[CONST_CALL_ATTR:[0-9]+]]
//
// OGCG-LABEL: define dso_local void @_Z10call_constv(
// OGCG: call void %{{.+}}(ptr dead_on_unwind writable sret(%struct.S) align 4 %{{.+}}) #[[OG_CONST_CALL_ATTR:[0-9]+]]

// CIR-LABEL: define dso_local void @_Z9call_purev(
// CIR: call void %{{.+}}(ptr dead_on_unwind writable sret(%struct.S) align 4 %{{.+}}) #[[PURE_CALL_ATTR:[0-9]+]]
//
// OGCG-LABEL: define dso_local void @_Z9call_purev(
// OGCG: call void %{{.+}}(ptr dead_on_unwind writable sret(%struct.S) align 4 %{{.+}}) #[[OG_PURE_CALL_ATTR:[0-9]+]]

// CIR-DAG: attributes #[[CONST_CALL_ATTR]] = { nounwind willreturn memory{{.*argmem: readwrite.*}} }
// CIR-DAG: attributes #[[PURE_CALL_ATTR]] = { nounwind willreturn memory{{.*read, argmem: readwrite.*}} }
// OGCG-DAG: attributes #[[OG_CONST_CALL_ATTR]] = { nounwind willreturn memory{{.*argmem: readwrite.*}} }
// OGCG-DAG: attributes #[[OG_PURE_CALL_ATTR]] = { nounwind willreturn memory{{.*read, argmem: readwrite.*}} }
