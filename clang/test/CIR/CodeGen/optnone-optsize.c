// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -Oz %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -Oz %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s -check-prefix=OGCG

// When compiling with -Oz, optnone functions should not have optsize or
// minsize. Previously CIR did not strip optsize from optnone functions,
// causing: "Attributes 'optsize and optnone' are incompatible!"

int foo1(int);

__attribute__((optnone))
int foo1(int a) {
    return a + 1;
}

int foo2(int a) {
  return foo1(a + 2);
}

// Both CIR and OGCG emit foo1 before foo2 with this source order.
// foo1 has optnone — its attributes should NOT include optsize.
// foo2 is normal — its attributes should include optsize from -Oz.

// LLVM: define {{.*}} i32 @foo1({{.*}}) {{.*}}#[[#ATTR1:]]
// LLVM: define {{.*}} i32 @foo2({{.*}}) {{.*}}#[[#ATTR2:]]
// LLVM-DAG: attributes #[[#ATTR1]] = {{{.*}}optnone{{.*}}}
// LLVM-DAG: attributes #[[#ATTR2]] = {{{.*}}optsize{{.*}}}

// OGCG: define {{.*}} i32 @foo1({{.*}}) {{.*}}#[[#ATTR1:]]
// OGCG: define {{.*}} i32 @foo2({{.*}}) {{.*}}#[[#ATTR2:]]
// OGCG-DAG: attributes #[[#ATTR1]] = {{{.*}}optnone{{.*}}}
// OGCG-DAG: attributes #[[#ATTR2]] = {{{.*}}optsize{{.*}}}
