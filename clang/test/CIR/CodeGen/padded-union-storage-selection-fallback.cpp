// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-ogcg.ll %s

// Verify padded-union storage member selection still follows CIRGen/OGCG
// fallback heuristic (highest alignment, tie-break on largest size) when
// explicit alignment inflates layout and prevents the size-based path.
//
// Here, alignas(64) forces total layout above member-derived layout and
// triggers fallback in RecordType::getStorageMember.
// Among members with alignment 8, S16 (size 16) must be selected over s8
// (size 8), while big (size 48) has alignment 1 and must not be selected.

struct S16 {
  long long a;
  long long b;
};

union alignas(64) U {
  long long s8;
  S16 s16;
  char big[48];
};

U g;

// CIR-DAG: !rec_U = !cir.record<union "U" padded {!s64i, !rec_S16, !cir.array<!s8i x 48>, !cir.array<!u8i x 48>}>
// CIR: cir.global external @g = #cir.zero : !rec_U {alignment = 64 : i64}

// LLVM: %union.U = type { %struct.S16, [48 x i8] }
// OGCG: %union.U = type { %struct.S16, [48 x i8] }
