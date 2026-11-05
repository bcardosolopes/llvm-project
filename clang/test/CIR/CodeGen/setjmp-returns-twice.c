// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// _setjmp call site must have returns_twice attribute.

typedef struct __jmp_buf_tag { int a[100]; } jmp_buf[1];
int _setjmp(jmp_buf);

void test() {
  jmp_buf buf;
  _setjmp(buf);
}

// The call to _setjmp should have a returns_twice attribute group.
// CIR: call i32 @_setjmp({{.*}}) #[[ATTR:[0-9]+]]
// CIR: attributes #[[ATTR]] = {{{.*}}returns_twice{{.*}}}

// OGCG: call i32 @_setjmp({{.*}}) #[[ATTR:[0-9]+]]
// OGCG: attributes #[[ATTR]] = {{{.*}}returns_twice{{.*}}}
