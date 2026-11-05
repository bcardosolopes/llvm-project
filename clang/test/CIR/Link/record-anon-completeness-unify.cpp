// Anonymous members unify across nested completeness views: an anonymous
// union whose members one input leaves incomplete (bare declaration
// references) and another completes is the same type, and must share one
// canonical -- not split its parents. The new constant interpreter
// miscompiled when 14 Holder-like views of the same union split Block,
// Pointer, Descriptor, IntPointer and BlockPointer apart.
//
// The drill routes pristine anonymous records through the unnamed subshape
// path (member incompleteness excuses there exactly as for named records),
// the layout gate spells anonymous names blind, and canonicalization
// reuses a canonical whose body matches modulo completeness instead of
// minting one per completeness view.
//
// The negative half pins the other direction: genuinely different member
// bodies, complete on both sides, still split.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/a.cpp -o %t/a.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/b.cpp -o %t/b.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/c.cpp -o %t/c.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/d.cpp -o %t/d.o
//
// RUN: cir-link %t/a.o %t/b.o -o %t/unified.cir \
// RUN: --warn-odr-divergence > %t/unified.txt 2>&1
// RUN: FileCheck %s < %t/unified.txt
// CHECK-NOT: renamed to
// CHECK: 0 records renamed
//
// One input per window: b.o's Holder is then checked against a.o's
// transferred shape, which names the anonymous union's canonical. The split
// fixpoint has to see the anonymous member on b.o's side too, or the lists
// differ in length and Holder splits on completeness alone.
// RUN: cir-link -j 1 %t/a.o %t/b.o -o %t/windows.cir \
// RUN: --warn-odr-divergence > %t/windows.txt 2>&1
// RUN: FileCheck %s < %t/windows.txt
//
// RUN: cir-link %t/c.o %t/d.o -o %t/split.cir \
// RUN: --warn-odr-divergence > %t/split.txt 2>&1
// RUN: FileCheck %s --check-prefix=SPLIT < %t/split.txt
// SPLIT-DAG: record 'Holder' {{.*}} renamed to 'Holder.m1'

//--- a.cpp
struct S;
struct Holder {
  union {
    S *p;
    int x;
  };
  int t;
};
Holder h;

//--- b.cpp
struct S {
  int v;
};
struct Holder {
  union {
    S *p;
    int x;
  };
  int t;
};
int useB(Holder *h) { return h->x + h->t; }

//--- c.cpp
struct S {
  int v;
};
struct Holder {
  union {
    S *p;
    int x;
  };
  int t;
};
int useC(Holder *h) { return h->x + h->t; }

//--- d.cpp
struct S {
  long v;
};
struct Holder {
  union {
    S *p;
    int x;
  };
  int t;
};
int useD(Holder *h) { return h->x + h->t; }
