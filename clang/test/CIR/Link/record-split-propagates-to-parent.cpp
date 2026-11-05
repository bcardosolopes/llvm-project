// Split propagation: a parent that unifies must still split when a nested
// record splits.
//
// Gadget genuinely differs (int vs long serial), so it splits. Widget's
// layout matches, but its nested Gadget splits, so the drill splits Widget
// too and the link succeeds with both variants.
//
// The int-vs-long difference is also what the layout gate is for: without
// it the drill would excuse Gadget through the unrelated incomplete Other
// and silently merge two different layouts. A Gadget that unifies while
// its serial differs is the miscompile this test guards.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/a.cpp -o %t/a.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/b.cpp -o %t/b.o
//
// RUN: cir-link %t/a.o %t/b.o -o %t/merged.cir \
// RUN: --warn-odr-divergence > %t/warnings.txt 2>&1
// RUN: FileCheck %s < %t/warnings.txt
// CHECK-DAG: record 'Gadget' {{.*}} renamed to 'Gadget.m1'
// CHECK-DAG: record 'Widget' {{.*}} renamed to 'Widget.m1'

//--- a.cpp
struct Gadget;
struct Widget;
struct Other {
  int tag;
};
struct Gadget {
  Widget *owner;
  int serial;
};
struct Widget {
  Gadget *part;
  Other *aux;
  int flags;
};
int useA(Widget *w) { return w->part->serial + w->aux->tag + w->flags; }

//--- b.cpp
struct Gadget;
struct Widget;
struct Other;
struct Gadget {
  Widget *owner;
  long serial;
};
struct Widget {
  Gadget *part;
  Other *aux;
  int flags;
};
int useB(Widget *w) { return (int)w->part->serial + w->flags; }
