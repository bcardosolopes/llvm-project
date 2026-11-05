// REQUIRES: x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -mcmodel=medium -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=IR-DEFAULT
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -mcmodel=medium %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG-DEFAULT

// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -mcmodel=medium -mlarge-data-threshold=200 -fclangir %s -o %t.cir.custom.ll
// RUN: FileCheck --input-file=%t.cir.custom.ll %s -check-prefix=IR-CUSTOM
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -mcmodel=medium -mlarge-data-threshold=200 %s -o %t.og.custom.ll
// RUN: FileCheck --input-file=%t.og.custom.ll %s -check-prefix=OGCG-CUSTOM

// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -mcmodel=large -mlarge-data-threshold=200 -fclangir %s -o %t.cir.large.ll
// RUN: FileCheck --input-file=%t.cir.large.ll %s -check-prefix=IR-LARGE
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -mcmodel=large -mlarge-data-threshold=200 %s -o %t.og.large.ll
// RUN: FileCheck --input-file=%t.og.large.ll %s -check-prefix=OGCG-LARGE

// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -mcmodel=small -mlarge-data-threshold=200 -fclangir %s -o %t.cir.small.ll
// RUN: FileCheck --input-file=%t.cir.small.ll %s -check-prefix=IR-SMALL
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -mcmodel=small -mlarge-data-threshold=200 %s -o %t.og.small.ll
// RUN: FileCheck --input-file=%t.og.small.ll %s -check-prefix=OGCG-SMALL

// Medium code model with default threshold.
// IR-DEFAULT: !{i32 1, !"Code Model", i32 3}
// IR-DEFAULT: !{i32 1, !"Large Data Threshold", i64 0}
// OGCG-DEFAULT: !{i32 1, !"Code Model", i32 3}
// OGCG-DEFAULT: !{i32 1, !"Large Data Threshold", i64 0}

// Medium code model with custom threshold.
// IR-CUSTOM: !{i32 1, !"Code Model", i32 3}
// IR-CUSTOM: !{i32 1, !"Large Data Threshold", i64 200}
// OGCG-CUSTOM: !{i32 1, !"Code Model", i32 3}
// OGCG-CUSTOM: !{i32 1, !"Large Data Threshold", i64 200}

// Large code model with custom threshold.
// IR-LARGE: !{i32 1, !"Code Model", i32 4}
// IR-LARGE: !{i32 1, !"Large Data Threshold", i64 200}
// OGCG-LARGE: !{i32 1, !"Code Model", i32 4}
// OGCG-LARGE: !{i32 1, !"Large Data Threshold", i64 200}

// Small code model — no large data threshold emitted.
// IR-SMALL: !{i32 1, !"Code Model", i32 1}
// IR-SMALL-NOT: !"Large Data Threshold"
// OGCG-SMALL: !{i32 1, !"Code Model", i32 1}
// OGCG-SMALL-NOT: !"Large Data Threshold"

static int i;

int f() {
    return i;
}
