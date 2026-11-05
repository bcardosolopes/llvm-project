// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that virtual base class casts include a null check before
// dereferencing the vtable pointer.

struct A { int a; virtual int aa(); };
struct B { int b; virtual int bb(); };
struct C : virtual A, virtual B { int c; virtual int aa(); virtual int bb(); };

extern C *x;

A *a() { return x; }

// CIR: cir.func {{.*}} @_Z1av
// CIR:   %[[PTR:.*]] = cir.load {{.*}} : !cir.ptr<!cir.ptr<!rec_C>>, !cir.ptr<!rec_C>
// CIR:   %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!rec_C>
// CIR:   %[[CMP:.*]] = cir.cmp(ne, %[[PTR]], %[[NULL]]) : !cir.ptr<!rec_C>, !cir.bool
// CIR:   %[[RESULT:.*]] = cir.ternary(%[[CMP]], true {
// CIR:     cir.vtable.get_vptr %[[PTR]]
// CIR:     cir.yield %{{.*}} : !cir.ptr<!rec_A>
// CIR:   }, false {
// CIR:     %[[NULLRES:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!rec_A>
// CIR:     cir.yield %[[NULLRES]] : !cir.ptr<!rec_A>
// CIR:   }) : (!cir.bool) -> !cir.ptr<!rec_A>

// LLVM: define {{.*}} @_Z1av
// LLVM:   %[[LOAD:.*]] = load ptr, ptr @x, align 8
// LLVM:   %[[CMP:.*]] = icmp ne ptr %[[LOAD]], null
// LLVM:   br i1 %[[CMP]], label %[[NOTNULL:.*]], label %[[ISNULL:.*]]
// LLVM: [[NOTNULL]]:
// LLVM:   %[[VTABLE:.*]] = load ptr, ptr %[[LOAD]], align 8
// LLVM:   %[[OFFPTR:.*]] = getelementptr i8, ptr %[[VTABLE]], i64 -24
// LLVM:   %[[OFF:.*]] = load i64, ptr %[[OFFPTR]], align 8
// LLVM:   %[[RESULT:.*]] = getelementptr inbounds i8, ptr %[[LOAD]], i64 %[[OFF]]
// LLVM: [[ISNULL]]:
// LLVM: [[MERGE:.*]]:
// LLVM:   %[[PHI:.*]] = phi ptr [ null, %[[ISNULL]] ], [ %[[RESULT]], %[[NOTNULL]] ]
