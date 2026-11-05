// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-error=return-type -Wno-deprecated-non-prototype -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-error=return-type -Wno-deprecated-non-prototype -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test that a K&R function declared with no prototype then defined with
// a parameter compiles without crashing (regression test).

static int bar();
void foo() {
  int a = bar();
}
int bar(unsigned a) {
}

// LLVM: define dso_local void @foo()
// LLVM:   call i32 (...) @bar()
// LLVM: define internal i32 @bar(i32 noundef %{{.*}})

// OGCG: define dso_local void @foo()
// OGCG:   call i32 (...) @bar()
// OGCG: define internal i32 @bar(i32 noundef %{{.*}})
