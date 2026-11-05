// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fexceptions -fcxx-exceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Failed dynamic_cast<D2&> must throw std::bad_cast as an invoke (not
// a plain call) so the exception can be caught by a surrounding try-catch.

struct Base { virtual ~Base() = default; };
struct D1 : Base {};
struct D2 : Base {};

int catch_bad_cast() {
    D1 d;
    Base& ref = d;
    try {
        D2& r = dynamic_cast<D2&>(ref);
        (void)r;
        return 1;
    } catch (...) {
        return 0;
    }
}

// Verify __cxa_bad_cast is emitted as an invoke with a landing pad.
// CHECK: invoke void @__cxa_bad_cast()
// CHECK-NEXT: to label %{{.*}} unwind label %[[LP:[0-9]+]]
// CHECK: [[LP]]:
// CHECK: landingpad
