// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Global initializer with address of union member array element.
// The CIR global_view indices traverse a union type, which must be lowered
// to a byte-offset GEP since LLVM unions are flattened to their largest member.

union reg {
    unsigned char b[2][2];
    unsigned short w[2];
    unsigned int d;
};
struct cpu { union reg pc; };
extern struct cpu cpu;
struct svar { void *ptr; };

struct svar svars[] = {
    { &((cpu.pc).b[0][1]) }
};

// CIR: cir.global external @svars
// CIR-SAME: #cir.global_view<@cpu, [0 : i32, 0 : i32, 0 : i32, 1 : i32]>

// LLVM: @svars = global [1 x %struct.svar] [%struct.svar { ptr getelementptr {{.*}}(i8, ptr @cpu, i64 1) }]

// OGCG: @svars = global [1 x %struct.svar] [%struct.svar { ptr getelementptr {{.*}}(i8, ptr @cpu, i64 1) }]
