// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// CHECK: cir.global external @a = #cir.zero : !rec_A
// CHECK: cir.global external @vol_a = #cir.zero : !rec_A

typedef struct {} S;

typedef struct {
    int a;
    int b;
    S s;
} A;

// CHECK: cir.func {{.*}} @foo1
// CHECK:   [[TMP0:%.*]] = cir.alloca !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>, ["a1", init]
// CHECK:   [[TMP1:%.*]] = cir.alloca !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>, ["a2", init]
// CHECK:   cir.store{{.*}} %arg0, [[TMP0]] : !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>
// CHECK:   cir.store{{.*}} %arg1, [[TMP1]] : !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>
// CHECK:   [[TMP2:%.*]] = cir.const #cir.int<1> : !s32i
// CHECK:   [[TMP3:%.*]] = cir.load{{.*}} [[TMP0]] : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>
// CHECK:   [[TMP4:%.*]] = cir.ptr_stride inbounds [[TMP3]], [[TMP2]] : (!cir.ptr<!rec_A>, !s32i) -> !cir.ptr<!rec_A>
// CHECK:   [[TMP5:%.*]] = cir.const #cir.int<1> : !s32i
// CHECK:   [[TMP6:%.*]] = cir.load{{.*}} [[TMP1]] : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>
// CHECK:   [[TMP7:%.*]] = cir.ptr_stride inbounds [[TMP6]], [[TMP5]] : (!cir.ptr<!rec_A>, !s32i) -> !cir.ptr<!rec_A>
// CHECK:   cir.copy [[TMP7]] to [[TMP4]] {{.*}}: !cir.ptr<!rec_A>
void foo1(A* a1, A* a2) {
    a1[1] = a2[1];
}

// CHECK: cir.func {{.*}} @foo2
// CHECK:    [[TMP0:%.*]] = cir.alloca !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>, ["a1", init]
// CHECK:    [[TMP1:%.*]] = cir.alloca !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>, ["a2", init]
// CHECK:    cir.store{{.*}} %arg0, [[TMP0]] : !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>
// CHECK:    cir.store{{.*}} %arg1, [[TMP1]] : !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>
// CHECK:    [[TMP2:%.*]] = cir.load{{.*}} [[TMP0]] : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>
// CHECK:    [[TMP2B:%.*]] = cir.cast bitcast [[TMP2]] : !cir.ptr<!rec_A> -> !cir.ptr<!u8i>
// CHECK:    [[TMP2C:%.*]] = cir.const #cir.int<8> : !s64i
// CHECK:    [[TMP2D:%.*]] = cir.ptr_stride inbounds [[TMP2B]], [[TMP2C]]
// CHECK:    [[TMP3:%.*]] = cir.cast bitcast [[TMP2D]] : !cir.ptr<!u8i> -> !cir.ptr<!rec_S>
// CHECK:    [[TMP4:%.*]] = cir.load{{.*}} [[TMP1]] : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>
// CHECK:    [[TMP4B:%.*]] = cir.cast bitcast [[TMP4]] : !cir.ptr<!rec_A> -> !cir.ptr<!u8i>
// CHECK:    [[TMP4C:%.*]] = cir.const #cir.int<8> : !s64i
// CHECK:    [[TMP4D:%.*]] = cir.ptr_stride inbounds [[TMP4B]], [[TMP4C]]
// CHECK:    [[TMP5:%.*]] = cir.cast bitcast [[TMP4D]] : !cir.ptr<!u8i> -> !cir.ptr<!rec_S>
// CHECK:    cir.copy [[TMP5]] to [[TMP3]] {{.*}}: !cir.ptr<!rec_S>
void foo2(A* a1, A* a2) {
    a1->s = a2->s;
}

// CHECK: cir.func {{.*}} @foo3() -> !u64i
// CHECK:    [[TMP0:%.*]] = cir.alloca !rec_A, !cir.ptr<!rec_A>, ["__retval"] {alignment = 4 : i64}
// CHECK:    [[TMP1:%.*]] = cir.get_global @a : !cir.ptr<!rec_A>
// CHECK:    cir.copy [[TMP1]] to [[TMP0]] {{.*}}: !cir.ptr<!rec_A>
// CHECK:    [[TMP3:%.*]] = cir.cast bitcast [[TMP0]] : !cir.ptr<!rec_A> -> !cir.ptr<!u64i>
// CHECK:    [[TMP4:%.*]] = cir.load align(4) [[TMP3]] : !cir.ptr<!u64i>, !u64i
// CHECK:    cir.return [[TMP4]] : !u64i
A a;
A foo3(void) {
    return a;
}

// CHECK: cir.func {{.*}} @foo4
// CHECK:    [[TMP0:%.*]] = cir.alloca !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>, ["a1", init]
// CHECK:    [[TMP1:%.*]] = cir.alloca !rec_A, !cir.ptr<!rec_A>, ["a2", init]
// CHECK:    cir.store{{.*}} %arg0, [[TMP0]] : !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>
// CHECK:    [[TMP2:%.*]] = cir.load deref{{.*}}  [[TMP0]] : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>
// CHECK:    cir.copy [[TMP2]] to [[TMP1]] {{.*}}: !cir.ptr<!rec_A>
void foo4(A* a1) {
    A a2 = *a1;
}

A create() { A a; return a; }

// CHECK: cir.func {{.*@foo5}}
// CHECK:   [[A:%.*]] = cir.alloca !rec_A, !cir.ptr<!rec_A>, ["a"]
// CHECK:   [[TMP:%.*]] = cir.alloca !rec_A, !cir.ptr<!rec_A>, ["tmp"]
// CHECK:   [[RET:%.*]] = cir.call @create() : () -> !u64i
// CHECK:   [[TMP_CAST:%.*]] = cir.cast bitcast [[TMP]] : !cir.ptr<!rec_A> -> !cir.ptr<!u64i>
// CHECK:   cir.store{{.*}} [[RET]], [[TMP_CAST]] : !u64i, !cir.ptr<!u64i>
// CHECK:   cir.copy [[TMP]] to [[A]]
void foo5() {
    A a;
    a = create();
}

void foo6(A* a1) {
  A a2 = (*a1);
// CHECK: cir.func {{.*@foo6}}
// CHECK:   [[TMP0:%.*]] = cir.alloca !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>, ["a1", init] {alignment = 8 : i64}
// CHECK:   [[TMP1:%.*]] = cir.alloca !rec_A, !cir.ptr<!rec_A>, ["a2", init] {alignment = 4 : i64}
// CHECK:   cir.store{{.*}} %arg0, [[TMP0]] : !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>
// CHECK:   [[TMP2:%.*]] = cir.load deref{{.*}}  [[TMP0]] : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>
// CHECK:   cir.copy [[TMP2]] to [[TMP1]] {{.*}}: !cir.ptr<!rec_A>
}

volatile A vol_a;
A foo7() {
  return vol_a;
}
// CHECK: cir.func {{.*@foo7}}() -> !u64i
// CHECK:   %0 = cir.alloca
// CHECK:   %1 = cir.get_global @vol_a
// CHECK:   cir.copy %1 to %0 volatile
