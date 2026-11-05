// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test that a function alias targeting an internal (static) function
// correctly emits both the alias and the aliasee function definition,
// and propagates hidden visibility (regression test).

static void foo(void) {}
void bar(void) __attribute__((alias("foo")))
__attribute__((visibility("hidden")));

// CIR-DAG: cir.func {{.*}}internal{{.*}}dso_local @foo()
// CIR-DAG: cir.alias external dso_local @bar = @foo : !cir.func<()> {global_visibility = #cir<visibility hidden>}

// LLVM: @bar = hidden alias void (), ptr @foo
// LLVM: define internal void @foo()

// OGCG: @bar = hidden alias void (), ptr @foo
// OGCG: define internal void @foo()
