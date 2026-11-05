// Completeness-tolerant record unification: two inputs hold the same class,
// one with a nested record completed and one with only a declaration, and
// the merge unifies them instead of splitting the parent.
//
// The digest hashes nested records by body, so the parent digests differ.
// The parent must still unify: the layouts are identical, and splitting
// leaves constructor and destructor disagreeing about which variant an
// object has.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/complete.cpp -o %t/complete.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/incomplete.cpp -o %t/incomplete.o
//
// The merge unifies Holder: no divergence warning, exit success.
// RUN: cir-link %t/complete.o %t/incomplete.o -o %t/merged.cir \
// RUN: --warn-odr-divergence > %t/warnings.txt 2>&1
// RUN: FileCheck %s --allow-empty < %t/warnings.txt
// CHECK-NOT: different body

//--- complete.cpp
struct Nested {
  int value;
};
struct Holder {
  Nested *ptr;
  int tag;
};
int useComplete(Holder *h) { return h->ptr->value + h->tag; }

//--- incomplete.cpp
struct Nested;
struct Holder {
  Nested *ptr;
  int tag;
};
int useIncomplete(Holder *h) { return h->tag; }
