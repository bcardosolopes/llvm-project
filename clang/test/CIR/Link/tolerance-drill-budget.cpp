// Tolerance-drill visit budget: a drill past its budget fails closed
// (splits) instead of hanging, logs the trip, and still produces the same
// split outcome as the untripped run.
//
// Widget contains Gadget contains Sprocket by value; Sprocket genuinely
// differs (int vs long teeth), so all three split. With the default budget
// the Widget drill completes untripped. With --tolerance-drill-budget=2
// the 3-visit Widget drill trips: the link still succeeds with the identical
// three renames, proving the trip is fail-closed, not a different verdict.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/a.cpp -o %t/a.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/b.cpp -o %t/b.o
//
// RUN: cir-link %t/a.o %t/b.o -o %t/tripped.cir \
// RUN: --tolerance-drill-budget=2 --warn-odr-divergence > %t/tripped.txt 2>&1
// RUN: FileCheck %s < %t/tripped.txt
// CHECK: tolerance drill for record 'Widget' exceeded its visit budget; splitting
// CHECK-DAG: record 'Widget' {{.*}} renamed to 'Widget.m1'
// CHECK-DAG: record 'Gadget' {{.*}} renamed to 'Gadget.m1'
// CHECK-DAG: record 'Sprocket' {{.*}} renamed to 'Sprocket.m1'
//
// RUN: cir-link %t/a.o %t/b.o -o %t/default.cir \
// RUN: --warn-odr-divergence > %t/default.txt 2>&1
// RUN: FileCheck %s --check-prefix=DEFAULT < %t/default.txt
// DEFAULT-NOT: exceeded its visit budget
// DEFAULT-DAG: record 'Widget' {{.*}} renamed to 'Widget.m1'
// DEFAULT-DAG: record 'Gadget' {{.*}} renamed to 'Gadget.m1'
// DEFAULT-DAG: record 'Sprocket' {{.*}} renamed to 'Sprocket.m1'

//--- a.cpp
struct Sprocket {
  int teeth;
};
struct Gadget {
  Sprocket gear;
  int serial;
};
struct Widget {
  Gadget part;
  int flags;
};
int useA(Widget *w) { return w->part.gear.teeth + w->part.serial + w->flags; }

//--- b.cpp
struct Sprocket {
  long teeth;
};
struct Gadget {
  Sprocket gear;
  int serial;
};
struct Widget {
  Gadget part;
  int flags;
};
int useB(Widget *w) { return (int)w->part.gear.teeth + w->part.serial + w->flags; }
