// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir %s

// Verify that a break inside a compound statement (scope) within a switch
// case is detected as a terminator at the AST level, preventing dead code
// (like guard-macro error calls) from ever being emitted. This matches
// OGCG behavior where HaveInsertPoint() returns false after a terminator.
// The emit-obj RUN line verifies the backend does not fire a dontcall-error
// diagnostic.

__attribute__((error("guard trap")))
void guard_trap(void);

int test(int type) {
    int result = 0;
    switch (type) {
        case 1: {
            int *p = (int*)(long)type;
            if (p == 0) {
                { break; }
                guard_trap();
            }
            result = *p;
            break;
        }
        case 2:
            result = 42;
            break;
    }
    return result;
}

// The guard_trap call must NOT appear in the CIR at all — the compound-stmt
// terminator detection in CIRGenStmt eliminates dead code at emission time.
//
// CHECK-LABEL: cir.func {{.*}}@_Z4testi
// CHECK-NOT:   cir.call @_Z10guard_trapv
// CHECK:       cir.return
