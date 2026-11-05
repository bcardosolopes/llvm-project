// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Test that the runtime filler loop in new[] correctly has partial array
// cleanup coverage when an element's initialization can throw.
// When the init-list phase pushes an irregular cleanup, the runtime loop
// must update endOfInit so the cleanup covers all initialized elements.

struct Inner { Inner(); ~Inner(); };

// S is an aggregate with a non-trivially-constructible member: the filler
// InitListExpr contains a CXXConstructExpr for Inner (can throw), so CIR
// routes through the runtime do-while loop rather than emitCXXAggrConstructorCall.
struct S {
    int x;
    Inner i;
    ~S();
};

// CIR-LABEL: @_Z4testi
// CIR: %[[EOFINIT:.*]] = cir.alloca {{.*}} ["arrayinit.endOfInit"]
// CIR: cir.store {{.*}}, %[[EOFINIT]]
// The runtime loop updates endOfInit on each iteration:
// CIR: cir.do {
// CIR:   cir.store {{.*}}, %[[EOFINIT]]

// LLVM-LABEL: define{{.*}} ptr @_Z4testi
// Verify the loop invokes the inner constructor and has a landing pad with
// cleanup that calls S's destructor for partially-constructed elements.
// LLVM: invoke void @_ZN5InnerC1Ev
// LLVM: landingpad
// LLVM:   cleanup
// LLVM: call void @_ZN1SD1Ev
S *test(int n) {
    return new S[n]{};
}
