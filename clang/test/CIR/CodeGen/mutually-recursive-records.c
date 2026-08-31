// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: cir-opt %t.cir -o %t.rt.cir
// RUN: cir-opt %t.rt.cir -o %t.rt2.cir
// RUN: diff %t.rt.cir %t.rt2.cir

// Mutually recursive records must each be emitted once, as their own alias
// definition, and referred to by name elsewhere. Expanding a nested record in
// place instead was unbounded: -emit-cir on sqlite3.c reached tens of GB.
//
// The cir-opt RUN lines matter as much as the CHECKs: a by-name reference is
// only correct if it re-parses and re-printing is a fixed point.

struct B;
struct A { struct B *b; int x; };
struct B { struct A *a; int y; };

struct A ga;
struct B gb;

// Each body appears once. Matched order-independently: which member of a cycle
// is defined first is not something this test should pin down.

// CHECK-DAG: !rec_A = !cir.struct<"A" {data !cir.ptr<{{(!rec_B|!cir.struct<"B">)}}>, data !s32i}>
// CHECK-DAG: !rec_B = !cir.struct<"B" {data !cir.ptr<{{(!rec_A|!cir.struct<"A">)}}>, data !s32i}>

// Self-recursive records were always handled and are unchanged.

struct Self { struct Self *next; int v; };
struct Self gs;

// CHECK-DAG: !rec_Self = !cir.struct<"Self" {data !cir.ptr<!cir.struct<"Self">>, data !s32i}>
