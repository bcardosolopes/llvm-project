// RUN: %clang_cc1 -triple aarch64-linux-android -Oz -emit-llvm -disable-llvm-passes -fclangir %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// A scope with two cleanups interleaves them in an order that is a layout
// detail, so each handler's call sites are counted in a pass of its own.
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=RELA
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=RELB

// CIRGen emits a scope's cleanups inline on every exit edge, since at that
// point each early return still lives in its own region. cir-tail-merge folds
// the copies back together once FlattenCFG has removed the regions, which is
// what keeps each cleanup handler down to the single call site classic codegen
// gives it -- with N call sites the -Oz inliner declines to inline the handler,
// so it survives out of line with a frame and an unwind table entry each.

void release_a(void *);
void release_b(void *);
int step(int);

void one_cleanup(int n) {
  int a __attribute__((cleanup(release_a))) = n;
  if (n == 1)
    return;
  if (n == 2)
    return;
  step(n);
}

// Three exits, one call site: the two early returns and the fallthrough all
// branch to the same block.
// CHECK-LABEL: define{{.*}} void @one_cleanup(
// CHECK:         call void @release_a(
// CHECK-NOT:     call void @release_a(
// CHECK:       }

void nested_cleanups(int n) {
  int a __attribute__((cleanup(release_a))) = n;
  if (n == 1)
    return;
  {
    int b __attribute__((cleanup(release_b))) = n;
    if (n == 2)
      return;
    if (step(n) < 0)
      return;
  }
  step(n + 1);
}

// Nesting gives the chain classic codegen builds. The two returns from inside
// b's scope share one release_b block, which falls into the single release_a
// block the outer return also branches to. Leaving the scope normally keeps its
// own release_b, since it carries on into the rest of the function rather than
// to the same place -- sharing that one too would take a dispatch variable.
// RELA-LABEL: define{{.*}} void @nested_cleanups(
// RELA-NOT:     call void @release_a(
// RELA:         call void @release_a(
// RELA-NOT:     call void @release_a(
// RELA:       }
// RELB-LABEL: define{{.*}} void @nested_cleanups(
// RELB-COUNT-2: call void @release_b(
// RELB-NOT:     call void @release_b(
// RELB:       }

// A tail is only merged when both copies compute it from the same values, so
// two cleanups on distinct objects stay distinct.
void distinct_objects(int n) {
  if (n == 1) {
    int a __attribute__((cleanup(release_a))) = n;
    step(a);
    return;
  }
  int b __attribute__((cleanup(release_a))) = n + 1;
  step(b);
}

// CHECK-LABEL: define{{.*}} void @distinct_objects(
// CHECK-COUNT-2: call void @release_a(
// CHECK-NOT:     call void @release_a(
// CHECK:       }

// A scope holding nothing but a trivially-destructed local still gets a cleanup
// on each exit edge -- the llvm.lifetime.end for it. Those cost no code, but
// the copies keep the exits in separate blocks, and SimplifyCFG will not fold
// two branches to a common destination across them. Sinking is what would
// normally clear them, and it does not fire here: the block they all reach also
// has a predecessor from outside the scope, which ends no lifetime at all. So
// the marker gets merged for the same reason a call does.
int consume(unsigned char *);

int scoped_marker(int n) {
  if (n) {
    unsigned char byte;
    do {
      if (consume(&byte) < 0)
        return -1;
      if (--n == 0)
        return -1;
    } while (byte);
    return 0;
  }
  return step(n);
}

// CHECK-LABEL: define{{.*}} i32 @scoped_marker(
// CHECK:         call void @llvm.lifetime.end.p0(
// CHECK-NOT:     call void @llvm.lifetime.end.p0(
// CHECK:       }
