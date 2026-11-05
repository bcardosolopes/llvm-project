// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -ftrapv -ftrapv-handler foo %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -ftrapv -ftrapv-handler foo %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -ftrapv -ftrapv-handler foo %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=OGCG

// Test that -ftrapv-handler calls the user-specified handler function on
// signed integer overflow instead of trapping.

// CIR: cir.func private @foo(!s64i, !s64i, !u8i, !u8i, ...) -> !s64i

// CIR-LABEL: @add
// CIR: %result, %overflow = cir.binop.overflow(add,
// CIR: cir.ternary(%overflow, true {
// CIR:   %[[LHS:.*]] = cir.cast integral {{.*}} : !s32i -> !s64i
// CIR:   %[[RHS:.*]] = cir.cast integral {{.*}} : !s32i -> !s64i
// CIR:   %[[OPID:.*]] = cir.const #cir.int<3> : !u8i
// CIR:   %[[BW:.*]] = cir.const #cir.int<32> : !u8i
// CIR:   %[[RET:.*]] = cir.call @foo(%[[LHS]], %[[RHS]], %[[OPID]], %[[BW]]) nothrow
// CIR:   %[[TRUNC:.*]] = cir.cast integral %[[RET]] : !s64i -> !s32i
// CIR:   cir.yield %[[TRUNC]]
// CIR: }, false {
// CIR:   cir.yield %result
// CIR: })
int add(int a, int b) { return a + b; }

// CIR-LABEL: @sub
// CIR: %result, %overflow = cir.binop.overflow(sub,
// CIR: cir.ternary(%overflow, true {
// CIR:   cir.const #cir.int<5> : !u8i
// CIR:   cir.call @foo(
// CIR: }, false {
// CIR:   cir.yield %result
// CIR: })
int sub(int a, int b) { return a - b; }

// CIR-LABEL: @mul
// CIR: %result, %overflow = cir.binop.overflow(mul,
// CIR: cir.ternary(%overflow, true {
// CIR:   cir.const #cir.int<7> : !u8i
// CIR:   cir.call @foo(
// CIR: }, false {
// CIR:   cir.yield %result
// CIR: })
int mul(int a, int b) { return a * b; }

// LLVM-LABEL: @add
// LLVM: %[[OVF:.*]] = call { i32, i1 } @llvm.sadd.with.overflow.i32({{.*}}), !nosanitize
// LLVM: %[[RES:.*]] = extractvalue { i32, i1 } %[[OVF]], 0, !nosanitize
// LLVM: %[[FLAG:.*]] = extractvalue { i32, i1 } %[[OVF]], 1, !nosanitize
// LLVM: br i1 %[[FLAG]], {{.*}}!nosanitize
// LLVM: sext i32 {{.*}} to i64, !nosanitize
// LLVM: sext i32 {{.*}} to i64, !nosanitize
// LLVM: call i64 (i64, i64, i8, i8, ...) @foo(i64 {{.*}}, i64 {{.*}}, i8 3, i8 32) #[[NOUNWIND:[0-9]+]], !nosanitize
// LLVM: trunc i64 {{.*}} to i32, !nosanitize
// LLVM: br label {{.*}}, !nosanitize

// LLVM-LABEL: @sub
// LLVM: call { i32, i1 } @llvm.ssub.with.overflow.i32({{.*}}), !nosanitize
// LLVM: br i1 {{.*}}, {{.*}}!nosanitize
// LLVM: call i64 (i64, i64, i8, i8, ...) @foo(i64 {{.*}}, i64 {{.*}}, i8 5, i8 32) #[[NOUNWIND]], !nosanitize

// LLVM-LABEL: @mul
// LLVM: call { i32, i1 } @llvm.smul.with.overflow.i32({{.*}}), !nosanitize
// LLVM: br i1 {{.*}}, {{.*}}!nosanitize
// LLVM: call i64 (i64, i64, i8, i8, ...) @foo(i64 {{.*}}, i64 {{.*}}, i8 7, i8 32) #[[NOUNWIND]], !nosanitize

// LLVM: attributes #[[NOUNWIND]] = { nounwind }

// OGCG-LABEL: @add
// OGCG: call { i32, i1 } @llvm.sadd.with.overflow.i32(
// OGCG: call i64 (i64, i64, i8, i8, ...) @foo(i64 {{.*}}, i64 {{.*}}, i8 3, i8 32)

// OGCG-LABEL: @sub
// OGCG: call { i32, i1 } @llvm.ssub.with.overflow.i32(
// OGCG: call i64 (i64, i64, i8, i8, ...) @foo(i64 {{.*}}, i64 {{.*}}, i8 5, i8 32)

// OGCG-LABEL: @mul
// OGCG: call { i32, i1 } @llvm.smul.with.overflow.i32(
// OGCG: call i64 (i64, i64, i8, i8, ...) @foo(i64 {{.*}}, i64 {{.*}}, i8 7, i8 32)
