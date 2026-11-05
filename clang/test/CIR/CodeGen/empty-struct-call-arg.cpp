// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s < %t.ogcg.ll

// Test that passing an empty struct to a function produces undef in CIR
// rather than loading from an uninitialized alloca. This is a CIR-specific
// concern because CIR defers ABI lowering: the empty struct parameter is
// present in the CIR function type but will be dropped by the target lowering
// pass (ABIArgInfo::Ignore).

struct Empty {};
struct NonEmpty { int x; };

int takeEmpty(Empty e);
int takeNonEmpty(NonEmpty ne);
int takeBoth(Empty e, NonEmpty ne);

// CIR-LABEL: @_Z6callerv
// CIR:         cir.const #cir.undef
// CIR:         cir.call @_Z9takeEmpty5Empty()
// CIR:         cir.call @_Z12takeNonEmpty8NonEmpty(
// CIR:         cir.const #cir.undef
// CIR:         cir.call @_Z8takeBoth5Empty8NonEmpty(

// LLVM-LABEL: define {{.*}}void @_Z6callerv
// LLVM:         call {{.*}}i32 @_Z9takeEmpty5Empty()
// LLVM:         call {{.*}}i32 @_Z12takeNonEmpty8NonEmpty(i32 {{.*}})
// LLVM:         call {{.*}}i32 @_Z8takeBoth5Empty8NonEmpty(i32 {{.*}})

// OGCG-LABEL: define {{.*}}void @_Z6callerv
// OGCG:         call {{.*}}i32 @_Z9takeEmpty5Empty()
// OGCG:         call {{.*}}i32 @_Z12takeNonEmpty8NonEmpty(i32 {{.*}})
// OGCG:         call {{.*}}i32 @_Z8takeBoth5Empty8NonEmpty(i32 {{.*}})
void caller() {
  takeEmpty(Empty{});
  takeNonEmpty(NonEmpty{1});
  takeBoth(Empty{}, NonEmpty{2});
}
