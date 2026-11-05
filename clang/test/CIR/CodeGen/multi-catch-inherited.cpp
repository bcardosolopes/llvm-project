// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify 3+ catch blocks with inherited exception types compile
// correctly. Tests the applyReplacements path for deep
// inheritance chains with using-constructors.

struct E0 {
    const char* msg;
    E0(const char* m) : msg(m) {}
    virtual ~E0() {}
};
struct E1 : E0 { using E0::E0; ~E1() override {} };
struct E2 : E1 { using E1::E1; ~E2() override {} };

// CHECK: define {{.*}} @_Z4testv()
// CHECK:   invoke void @_ZN2E2C{{.*}}(
// CHECK:   landingpad
int test() {
    try {
        throw E2("test");
    } catch (const E2&) {
        return 3;
    } catch (const E1&) {
        return 2;
    } catch (const E0&) {
        return 1;
    }
    return 0;
}
