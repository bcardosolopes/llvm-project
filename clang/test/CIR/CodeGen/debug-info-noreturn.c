// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c2x %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c2x %s -o - | FileCheck %s --check-prefix=OGCG

// A function marked noreturn (__attribute__((noreturn)), _Noreturn, or the C23
// [[noreturn]] attribute) carries DW_AT_noreturn in its DISubprogram, which
// LLVM models as DIFlagNoReturn. Classic CodeGen derives this from
// FunctionDecl::isNoReturn(); CIR previously dropped it when reconstructing the
// subprogram flags during lowering (it set DIFlagPrototyped but not NoReturn).

__attribute__((noreturn)) void die_attr(void) { __builtin_trap(); }

_Noreturn void die_kw(void) { __builtin_trap(); }

void normal(void) {}

// The noreturn functions carry DIFlagNoReturn (alongside DIFlagPrototyped); the
// plain function carries only DIFlagPrototyped.
// CHECK-DAG: !DISubprogram(name: "die_attr",{{.*}}flags: DIFlagPrototyped | DIFlagNoReturn,
// CHECK-DAG: !DISubprogram(name: "die_kw",{{.*}}flags: DIFlagPrototyped | DIFlagNoReturn,
// CHECK-DAG: !DISubprogram(name: "normal",{{.*}}flags: DIFlagPrototyped,

// OGCG-DAG: !DISubprogram(name: "die_attr",{{.*}}flags: DIFlagPrototyped | DIFlagNoReturn,
// OGCG-DAG: !DISubprogram(name: "die_kw",{{.*}}flags: DIFlagPrototyped | DIFlagNoReturn,
// OGCG-DAG: !DISubprogram(name: "normal",{{.*}}flags: DIFlagPrototyped,
