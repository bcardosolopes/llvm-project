// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR

// Source globals intentionally placed in llvm.* sections still carry
// source-level debug info in OGCG and direct-CIR should match that.

__attribute__((section("llvm.metadata"))) int meta = 7;
__attribute__((section("llvm.foo"))) int helper = 11;
int g = 42;

// OGCG: @meta = global i32 7, section "llvm.metadata", align 4, !dbg ![[META_EXPR:[0-9]+]]
// OGCG: @helper = global i32 11, section "llvm.foo", align 4, !dbg ![[HELPER_EXPR:[0-9]+]]
// OGCG: @g = global i32 42, align 4, !dbg ![[G_EXPR:[0-9]+]]
// OGCG-DAG: ![[META_EXPR]] = !DIGlobalVariableExpression(var: ![[META_VAR:[0-9]+]], expr: !DIExpression())
// OGCG-DAG: ![[META_VAR]] = distinct !DIGlobalVariable(name: "meta"
// OGCG-DAG: ![[HELPER_EXPR]] = !DIGlobalVariableExpression(var: ![[HELPER_VAR:[0-9]+]], expr: !DIExpression())
// OGCG-DAG: ![[HELPER_VAR]] = distinct !DIGlobalVariable(name: "helper"
// OGCG-DAG: ![[G_EXPR]] = !DIGlobalVariableExpression(var: ![[G_VAR:[0-9]+]], expr: !DIExpression())
// OGCG-DAG: ![[G_VAR]] = distinct !DIGlobalVariable(name: "g"

// CIR: @meta = global i32 7, section "llvm.metadata", align 4, !dbg ![[META_EXPR:[0-9]+]]
// CIR: @helper = global i32 11, section "llvm.foo", align 4, !dbg ![[HELPER_EXPR:[0-9]+]]
// CIR: @g = global i32 42, align 4, !dbg ![[G_EXPR:[0-9]+]]
// CIR-DAG: ![[META_EXPR]] = !DIGlobalVariableExpression(var: ![[META_VAR:[0-9]+]], expr: !DIExpression())
// CIR-DAG: ![[META_VAR]] = distinct !DIGlobalVariable(name: "meta"
// CIR-DAG: ![[HELPER_EXPR]] = !DIGlobalVariableExpression(var: ![[HELPER_VAR:[0-9]+]], expr: !DIExpression())
// CIR-DAG: ![[HELPER_VAR]] = distinct !DIGlobalVariable(name: "helper"
// CIR-DAG: ![[G_EXPR]] = !DIGlobalVariableExpression(var: ![[G_VAR:[0-9]+]], expr: !DIExpression())
// CIR-DAG: ![[G_VAR]] = distinct !DIGlobalVariable(name: "g"
