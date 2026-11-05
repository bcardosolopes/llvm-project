// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Verify that multiple cleanup destructor invoke-to-terminate calls within
// the same function share a single terminate landing pad, matching OGCG's
// behavior. This avoids duplicating identical landing pad blocks per call
// site. (T-SHARE-TERMINATE-LPAD)
//
// With 5 Throwing objects and 1 may_throw() call, the cleanup chain has
// 5 invoke-to-terminate destructor calls that must all share ONE terminate
// landing pad. Before the fix, each would get its own copy.

struct Throwing {
  ~Throwing() noexcept(false);
};

void may_throw();

void test_shared_lpad() {
  Throwing a, b, c, d, e;
  may_throw();
}

// Verify exactly ONE terminate landing pad (catch ptr null + call
// @__clang_call_terminate + unreachable) per function, despite multiple
// invoke-to-terminate sites.

// CIR-LABEL: define {{.*}} @_Z16test_shared_lpadv()
// CIR:       catch ptr null
// CIR:       call void @__clang_call_terminate(
// CIR-NEXT:  unreachable
// Verify no second "catch ptr null" in the function:
// CIR-NOT:   catch ptr null

// OGCG-LABEL: define {{.*}} @_Z16test_shared_lpadv()
// OGCG:       catch ptr null
// OGCG:       call void @__clang_call_terminate(
// OGCG-NEXT:  unreachable
// Verify no second "catch ptr null" in the function:
// OGCG-NOT:   catch ptr null
