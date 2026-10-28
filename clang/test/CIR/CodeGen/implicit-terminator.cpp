// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -Wno-return-type %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefixes=CIR,CIR-O0
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -Wno-return-type -O1 %s -o %t-O1.cir
// RUN: FileCheck --input-file=%t-O1.cir %s -check-prefixes=CIR,CIR-O1

// The terminator CIRGen synthesizes when control flows off the end of a
// function body is marked `implicit`; terminators written by the user are not.

void void_falloff() {}

// CIR-LABEL: cir.func {{.*}} @_Z12void_falloffv()
// CIR:         cir.return implicit

int nonvoid_falloff(int x) {
  if (x)
    return 1;
}

// CIR-LABEL: cir.func {{.*}} @_Z15nonvoid_falloffi(
// CIR:           cir.return %{{.*}} : !s32i
// CIR-O0:      cir.trap implicit
// CIR-O1:      cir.unreachable implicit

int explicit_return() { return 0; }

// CIR-LABEL: cir.func {{.*}} @_Z15explicit_returnv()
// CIR:         cir.return %{{.*}} : !s32i
// CIR-NOT:     implicit
// CIR:       }

int user_trap() { __builtin_trap(); }

// CIR-LABEL: cir.func {{.*}} @_Z9user_trapv()
// CIR:         cir.trap
// CIR-NOT:     implicit
// CIR:       }

int user_unreachable() { __builtin_unreachable(); }

// CIR-LABEL: cir.func {{.*}} @_Z16user_unreachablev()
// CIR:         cir.unreachable
// CIR-NOT:     implicit
// CIR:       }
