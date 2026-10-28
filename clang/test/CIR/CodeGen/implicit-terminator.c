// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -Wno-return-type %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR

// In C, flowing off the end of a non-void function returns normally, so the
// synthesized terminator is a `cir.return` marked `implicit`.

void void_falloff(void) {}

// CIR-LABEL: cir.func {{.*}} @void_falloff()
// CIR:         cir.return implicit

int nonvoid_falloff(int x) {
  if (x)
    return 1;
}

// CIR-LABEL: cir.func {{.*}} @nonvoid_falloff(
// CIR:           cir.return %{{.*}} : !s32i
// CIR:         %[[RV:.*]] = cir.load %{{.*}} : !cir.ptr<!s32i>, !s32i
// CIR-NEXT:    cir.return implicit %[[RV]] : !s32i

int explicit_return(void) { return 0; }

// CIR-LABEL: cir.func {{.*}} @explicit_return()
// CIR:         cir.return %{{.*}} : !s32i
// CIR-NOT:     implicit
// CIR:       }
