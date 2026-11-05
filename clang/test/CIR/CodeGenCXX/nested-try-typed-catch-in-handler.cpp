// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-obj %s -o %t.o
// Try with typed catch nested inside catch handler (was CIR verifier crash, throw-from-catch fix)
int main() {
    try { throw 1; }
    catch (int) {
        try { throw 2; }
        catch (int) {}
    }
    return 0;
}
