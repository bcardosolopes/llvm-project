// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O1 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Verify that !unpredictable metadata is emitted on branch and switch
// instructions when __builtin_unpredictable is used with -O1.

void foo(void);
void bar(void);

void test_unpredictable_if(int x) {
  if (__builtin_unpredictable(x > 0))
    foo();
  else
    bar();
}

// LLVM: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !unpredictable
// OGCG: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !unpredictable

void case0(void);
void case1(void);
void default_case(void);

void test_unpredictable_switch(int x) {
  switch (__builtin_unpredictable(x)) {
  case 0: case0(); break;
  case 1: case1(); break;
  default: default_case(); break;
  }
}

// LLVM: switch i32 %{{.*}}, label %{{.*}} [
// LLVM: ], !unpredictable
// OGCG: switch i32 %{{.*}}, label %{{.*}} [
// OGCG: ], !unpredictable
