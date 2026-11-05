// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fno-builtin -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t.cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fno-builtin -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s < %t.ogcg.ll

// Verify that well-known returns_twice functions (setjmp, vfork, etc.) get
// the returns_twice attribute even when -fno-builtin strips the builtin
// attribute. This is a safety net to ensure correct codegen.

typedef long jmp_buf[1];
int setjmp(jmp_buf);
int vfork(void);

void test(void) {
  jmp_buf buf;
  setjmp(buf);
  vfork();
}

// Both setjmp and vfork share the same attribute group with returns_twice.
// Declarations now appear after the function that references them.
// LLVM: declare {{.*}} @vfork({{.*}}) #[[RT_ATTR:[0-9]+]]
// LLVM: declare {{.*}} @setjmp({{.*}}) #[[RT_ATTR]]
// LLVM: attributes #[[RT_ATTR]] = {{{.*}}returns_twice{{.*}}}

// OGCG: declare {{.*}} @setjmp({{.*}}) #[[RT_ATTR:[0-9]+]]
// OGCG: declare {{.*}} @vfork({{.*}}) #[[RT_ATTR]]
// OGCG: attributes #[[RT_ATTR]] = {{{.*}}returns_twice{{.*}}}
