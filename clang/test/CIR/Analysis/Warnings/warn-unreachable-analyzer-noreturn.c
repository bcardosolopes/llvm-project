// RUN: %clang_cc1 -fsyntax-only -verify=expected,cfg -Wunreachable-code -Wreturn-type -Wimplicit-fallthrough %s
// RUN: %clang_cc1 -fclangir -emit-cir -fclangir-analysis-warnings=return-type -verify=expected,cir -Wunreachable-code -Wreturn-type -Wimplicit-fallthrough %s -o /dev/null

// Copy of clang/test/Sema/warn-unreachable-analyzer-noreturn.c, also run with
// -fclangir-analysis-warnings=return-type. Diagnostics only one engine
// reports use the cfg- or cir- prefix, with a TODO for ClangIR.
//
// TODO(cir): the -Wunreachable-code and -Wimplicit-fallthrough diagnostics in
// this test still come from Clang's CFG.

// 'analyzer_noreturn' calls can return, so the code after them is not
// unreachable. -Wreturn-type and -Wimplicit-fallthrough do treat them as
// noreturn.

void analyzer_stop(const char *) __attribute__((analyzer_noreturn));
void real_stop(void) __attribute__((noreturn));
void use(int);

void after_analyzer_noreturn(int x) {
  if (x)
    return;
  analyzer_stop("bad");
  use(1); // no warning
}

void analyzer_then_real(void) {
  analyzer_stop("bad");
  real_stop();
  use(1); // expected-warning {{code will never be executed}}
}

// TODO(cir): CIRGen does not mark calls to analyzer_noreturn functions, so the
// analysis does not treat them as noreturn.
int return_type(int x) {
  if (x)
    return 1;
  analyzer_stop("bad");
} // cir-warning {{non-void function does not return a value in all control paths}}

void fallthrough(int x) {
  switch (x) {
  case 1:
    analyzer_stop("bad");
  case 2: // no warning
    use(2);
    break;
  }
}
