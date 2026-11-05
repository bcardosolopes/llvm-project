// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -mconstructor-aliases -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -mconstructor-aliases -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-ogcg.ll %s

// The complete-object structors that get folded must be gone from every output,
// and CIR must agree with OGCG on which ones those are.
// RUN: FileCheck --check-prefix=FOLDED --input-file=%t.cir %s
// RUN: FileCheck --check-prefix=FOLDED --input-file=%t-cir.ll %s
// RUN: FileCheck --check-prefix=FOLDED --input-file=%t-ogcg.ll %s

// Without -mconstructor-aliases nothing is folded, and the tag still never
// reaches the output.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t-noalias.cir
// RUN: FileCheck --check-prefix=NOALIAS --input-file=%t-noalias.cir %s

// CIRGen tags a linkonce_odr complete-object structor that is a plain
// delegating stub with #cir.structor_dedup, and LoweringPrepare rewrites every
// reference to the base variant and erases the stub -- the same end state OGCG
// reaches through StructorCodegen::RAUW during CodeGen. Unlike OGCG, the fold
// happens after CXXABILowering and LoweringPrepare have materialized their own
// references, so this test pins down each carrier of a structor name that must
// be rewritten.

// The tag is internal to the pipeline.
// CIR-NOT: cir.structor_dedup
// NOALIAS-NOT: cir.structor_dedup

//===----------------------------------------------------------------------===//
// Vtable initializer
//===----------------------------------------------------------------------===//

struct Poly {
  virtual ~Poly() {}
  int x;
};

void use_poly() { Poly p; }

// The complete-object slot of the vtable points at D2 after the fold. The
// vtable is built by CXXABILowering, i.e. after CIRGen, which is exactly why
// the fold cannot happen during CIRGen.

// CIR-DAG: cir.global {{.*}}@_ZTV4Poly = #cir.vtable<{{{.*}}#cir.global_view<@_ZN4PolyD2Ev>{{.*}}#cir.global_view<@_ZN4PolyD0Ev>{{.*}}}>
// LLVM-DAG: @_ZTV4Poly = linkonce_odr unnamed_addr constant { [4 x ptr] } { [4 x ptr] [ptr null, ptr @_ZTI4Poly, ptr @_ZN4PolyD2Ev, ptr @_ZN4PolyD0Ev] }
// OGCG-DAG: @_ZTV4Poly = linkonce_odr unnamed_addr constant { [4 x ptr] } { [4 x ptr] [ptr null, ptr @_ZTI4Poly, ptr @_ZN4PolyD2Ev, ptr @_ZN4PolyD0Ev] }

// CIR-DAG:  cir.call @_ZN4PolyD2Ev(%{{.*}}) nothrow : (!cir.ptr<!rec_Poly>) -> ()
// LLVM-DAG:   call void @_ZN4PolyD2Ev(ptr {{[^%]*}}%{{.*}})
// OGCG-DAG:   call void @_ZN4PolyD2Ev(ptr {{.*}} %{{.*}})

// NOALIAS-DAG: cir.func {{.*}} @_ZN4PolyD1Ev
// NOALIAS-DAG: cir.global {{.*}}@_ZTV4Poly = #cir.vtable<{{{.*}}#cir.global_view<@_ZN4PolyD1Ev>{{.*}}}>

//===----------------------------------------------------------------------===//
// cir.throw destructor operand
//===----------------------------------------------------------------------===//

struct Thrown {
  ~Thrown() {}
  int v;
};

void do_throw() { throw Thrown{}; }

// CIR-DAG: cir.throw %{{.*}} : !cir.ptr<!rec_Thrown>, @_ZTI6Thrown, @_ZN6ThrownD2Ev
// LLVM-DAG:   call void @__cxa_throw(ptr %{{.*}}, ptr @_ZTI6Thrown, ptr @_ZN6ThrownD2Ev)
// OGCG-DAG:   call void @__cxa_throw(ptr %{{.*}}, ptr @_ZTI6Thrown, ptr @_ZN6ThrownD2Ev)

// NOALIAS-DAG: cir.throw %{{.*}} : !cir.ptr<!rec_Thrown>, @_ZTI6Thrown, @_ZN6ThrownD1Ev

//===----------------------------------------------------------------------===//
// __cxa_atexit registration for a static-storage object
//===----------------------------------------------------------------------===//

struct Static {
  ~Static() {}
  int v;
};

Static g;

// The registration is emitted by LoweringPrepare itself, from the destructor
// list it collects while running.

// CIR-DAG: %{{.*}} = cir.get_global @_ZN6StaticD2Ev : !cir.ptr<!cir.func<(!cir.ptr<!rec_Static>)>>
// LLVM-DAG:   call i32 @__cxa_atexit(ptr @_ZN6StaticD2Ev, ptr @g, ptr @__dso_handle)
// OGCG-DAG:   call i32 @__cxa_atexit(ptr @_ZN6StaticD2Ev, ptr @g, ptr @__dso_handle)

// NOALIAS-DAG: %{{.*}} = cir.get_global @_ZN6StaticD1Ev : !cir.ptr<!cir.func<(!cir.ptr<!rec_Static>)>>

//===----------------------------------------------------------------------===//
// Negative case: a virtual base makes D1 genuinely different from D2
//===----------------------------------------------------------------------===//

struct VBase { ~VBase(); };

struct HasVBase : virtual VBase {
  ~HasVBase() {}
};

void use_vbase() { HasVBase h; }

// D1 takes no VTT and calls D2 with one, so it is not a plain delegation and
// must survive -- in CIR exactly as in OGCG.

// CIR-DAG: cir.func {{.*}} @_ZN8HasVBaseD1Ev
// CIR-DAG:  cir.call @_ZN8HasVBaseD1Ev(%{{.*}}) nothrow : (!cir.ptr<!rec_HasVBase>) -> ()
// LLVM-DAG: define linkonce_odr void @_ZN8HasVBaseD1Ev(ptr
// LLVM-DAG:   call void @_ZN8HasVBaseD1Ev(ptr {{[^%]*}}%{{.*}})
// OGCG-DAG: define linkonce_odr void @_ZN8HasVBaseD1Ev(ptr
// OGCG-DAG:   call void @_ZN8HasVBaseD1Ev(ptr {{.*}} %{{.*}})

//===----------------------------------------------------------------------===//
// Nothing anywhere still names a folded structor
//===----------------------------------------------------------------------===//

// FOLDED-NOT: _ZN4PolyD1Ev
// FOLDED-NOT: _ZN6ThrownD1Ev
// FOLDED-NOT: _ZN6StaticD1Ev
