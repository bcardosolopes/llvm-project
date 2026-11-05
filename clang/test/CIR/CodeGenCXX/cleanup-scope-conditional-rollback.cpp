// A cir.cleanup.scope takes destructors twice: its own cleanup's when it forms,
// and later those of enclosing cleanups that form no scope of their own. The
// second batch cannot be vetted up front -- nothing about a cleanup's operands
// is known until it is emitted -- so a scope stays undoable until no cleanup
// left on the EH stack can still be appended to it, and the synthetic tries its
// shared pad makes redundant are only dropped at that point.
//
// This is what makes the undo necessary. A new-expression with a class-level
// nothrow operator new runs its initializer under a null check, so the member
// destructors that unwind a partially built object name addresses computed
// inside that cir.if. They are deactivated at the end of the initializer but
// stay on the stack until the full expression ends, so they are still there to
// be appended to a scope formed around the calls that follow -- which is a
// sibling of the cir.if, not a descendant of it. OGCG spills such addresses
// (DominatingValue::save); CIR has no equivalent, so the scope comes apart
// again and the range goes back to the per-call tries it still has, which do
// sit inside that region.
//
// RUN: %clang_cc1 -std=c++17 -triple aarch64-linux-android23 -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s \
// RUN:   --implicit-check-not=cir.cleanup.scope

struct Ref { Ref(); Ref(const Ref &); ~Ref(); };
struct Arr { Arr(Ref); ~Arr(); };
Ref makeRef();
Arr makeArr(Ref);

struct Shim {
  static void *operator new(unsigned long) noexcept;
  Ref self_;
  Arr queued_;
};

struct Owner { Owner(Shim *); ~Owner(); };
void use(Owner, Ref);

void f(Ref r) {
  use(Owner(new Shim{r, makeArr(makeRef())}), makeRef());
}

// CIR-LABEL: cir.func{{.*}} @_Z1f3Ref
// CIR:   %[[FLAG:[0-9]+]] = cir.alloca !cir.bool, {{.*}} ["cleanup.cond"]
// CIR:   %[[TMP:[0-9]+]] = cir.alloca !rec_Ref, {{.*}} ["agg.tmp1"]
// CIR:   %[[MEM:[0-9]+]] = cir.call @_ZN4ShimnwEm({{.*}}) nothrow
// CIR:   cir.if %{{[0-9]+}} {
// CIR:     %[[OBJ:[0-9]+]] = cir.cast bitcast %[[MEM]] : !cir.ptr<!void> -> !cir.ptr<!rec_Shim>
// CIR:     %[[SELF:[0-9]+]] = cir.cast bitcast %[[OBJ]] : !cir.ptr<!rec_Shim> -> !cir.ptr<!rec_Ref>
//
// The member destructor reaches the calls it guards through their per-call
// tries, which sit inside the same cir.if as the address it names.
// CIR:     cir.try synthetic
// CIR:       cir.call exception @_ZN3RefC1ERKS_(%[[SELF]]
// CIR:     cir.try synthetic
// CIR:       cir.call exception @_Z7makeRefv(%[[TMP]]
// CIR:     cir.try synthetic
// CIR:       cir.call exception @_Z7makeArr3Ref({{.*}}, %[[TMP]]
// CIR:         cir.call @_ZN3RefD1Ev(%[[TMP]])
// CIR:         %[[TEST:[0-9]+]] = cir.load {{.*}}%[[FLAG]]
// CIR:         cir.if %[[TEST]] {
// CIR:           cir.call @_ZN3RefD1Ev(%[[SELF]])
// CIR:         }
// CIR:         cir.call @_ZdlPvm(%[[MEM]]
// CIR:   } else {
//
// Nothing past the cir.if got a shared pad either: a scope formed there is
// undone as soon as the still-live member cleanup is appended to it, taking
// the scopes nested inside it along, so every call keeps its own try with the
// enclosing destructors spelled out in full.
// CIR:   cir.try synthetic
// CIR:     cir.call exception @_ZN5OwnerC1EP4Shim
// CIR:       cir.call @_ZN3RefD1Ev(%[[TMP]])
// CIR:   cir.try synthetic
// CIR:     cir.call exception @_Z7makeRefv
// CIR:       cir.call @_ZN5OwnerD1Ev
// CIR:       cir.call @_ZN3RefD1Ev(%[[TMP]])
// CIR:   cir.try synthetic
// CIR:     cir.call exception @_Z3use5Owner3Ref
// CIR:       cir.call @_ZN3RefD1Ev
// CIR:       cir.call @_ZN5OwnerD1Ev
// CIR:       cir.call @_ZN3RefD1Ev(%[[TMP]])
