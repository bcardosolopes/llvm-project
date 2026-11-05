// signext/zeroext on an Extend call argument is an ABI property, independent of
// noundef. For indirect/virtual calls the lowering previously nested the
// signext/zeroext emission inside the `if (enableNoundef && addNoUndef)` block,
// so -no-enable-noundef-analysis dropped it, producing wrong high bits at the
// call boundary. The direct-call path and the function prolog/return paths (and
// classic CodeGen) emit it unconditionally.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=DEFAULT --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -no-enable-noundef-analysis %s -o %t-no.ll
// RUN: FileCheck --check-prefix=DISABLED --input-file=%t-no.ll %s

int f(signed char);
int call(int (*fp)(signed char), signed char c) { return fp(c); }

// With noundef enabled, the indirect call carries both noundef and signext.
// DEFAULT-LABEL: define {{.*}}@call(
// DEFAULT:       call i32 %{{.*}}(i8 noundef signext %

// With noundef disabled, noundef is gone but signext must remain.
// DISABLED-LABEL: define {{.*}}@call(
// DISABLED:       call i32 %{{.*}}(i8 signext %
