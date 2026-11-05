// REQUIRES: x86-registered-target
// REQUIRES: system-linux
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: %clang --target=x86_64-unknown-linux-gnu -c %t.ll -o %t.o
// RUN: %clang --target=x86_64-unknown-linux-gnu -c %t.ogcg.ll -o %t.ogcg.o
// RUN: %clang --target=x86_64-unknown-linux-gnu %t.o -lstdc++ -o %t.exe
// RUN: %clang --target=x86_64-unknown-linux-gnu %t.ogcg.o -lstdc++ -o %t.ogcg.exe
// RUN: %t.ogcg.exe
// RUN: %t.exe
// Goto inside try block prematurely destroys RAII objects
static int alive = 0;
struct R { R() { alive++; } ~R() { alive--; } };
int main() {
    try {
        R r;
        goto label;
label:
        if (alive != 1) return 1;
    } catch (...) {}
    return 0;
}
