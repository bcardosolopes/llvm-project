// RUN: %clang_cc1 -Werror=return-type -triple i686-pc-openbsd -fsyntax-only -verify=expected,cfg -ffreestanding %s
// RUN: %clang_cc1 -fclangir -emit-cir -fclangir-analysis-warnings=return-type -Werror=return-type -triple i686-pc-openbsd -verify=expected,cir -ffreestanding %s -o /dev/null

// Copy of clang/test/Sema/freemain.c, also run with
// -fclangir-analysis-warnings=return-type. Diagnostics only one engine
// reports use the cfg- or cir- prefix, with a TODO for ClangIR.

// Tests that -ffreestanding disables all special treatment of main().

void* allocate(long size);

void* main(void* context, long size) {
  if (context) return allocate(size);
} // expected-error {{non-void function does not return a value in all control paths}}
