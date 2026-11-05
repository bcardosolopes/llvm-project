// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -DDEF -emit-llvm -o - %s | FileCheck %s --check-prefix=DEF
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -DDEF -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=DEF %}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -DREF -emit-llvm -o - %s | FileCheck %s --check-prefix=REF
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -DREF -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=REF %}

#ifdef DEF
int g;
int h = 1;

void sink(void) {}

// DEF: @g = global i32 0, align 4
// DEF: @h = global i32 1, align 4
// DEF-LABEL: define {{.*}}void @sink()
#endif

#ifdef REF
extern int g;

int f(void) { return g; }

// REF: @g = external global i32
// REF-LABEL: define {{.*}}i32 @f()
// REF: load i32, ptr @g, align 4
#endif
