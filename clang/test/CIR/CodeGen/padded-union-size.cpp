// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-ogcg.ll %s

// Test for padded unions where a low-alignment member is larger than the
// storageType selected by the alignment-first heuristic. This exercises the
// getSize() computation for padded unions, which must use the storageType
// (highest alignment member) + padding rather than max(all members) + padding.
//
// In the union below, AlignedBuf has CIR alignment 1 (its only member is
// array<u8 x 48>) but size 48, while SmallAligned has CIR alignment 8 and
// size 24. The storageType heuristic picks SmallAligned (higher alignment),
// and padding = 48 - 24 = 24. The correct union size is 24 + 24 = 48, not
// 48 + 24 = 72.

struct alignas(16) AlignedBuf {
  char data[48];
};

struct SmallAligned {
  void *ptrs[3]; // 24 bytes, alignment 8
};

union Data {
  void *small;
  SmallAligned medium;
  AlignedBuf large;
};

// The union should be 48 bytes (same as AlignedBuf, the physically largest
// member). The storageType is SmallAligned (alignment 8 > AlignedBuf's CIR
// alignment 1), with 24 bytes of padding to reach 48.
//
// CIR-DAG: !rec_Data = !cir.record<union "Data" padded {!cir.ptr<!void>, !rec_SmallAligned, !rec_AlignedBuf, !cir.array<!u8i x 24>}>

// Containing struct to verify that the union member doesn't cause extra
// inter-member padding from inconsistent size computation.
struct Container {
  Data d;
  int type;
};

// LLVM: %struct.Container = type { %union.Data, i32, [12 x i8] }
// LLVM: %union.Data = type { %struct.SmallAligned, [24 x i8] }
// OGCG: %struct.Container = type { %union.Data, i32, [12 x i8] }
// OGCG: %union.Data = type { %struct.SmallAligned, [24 x i8] }

// Array of containers to test that getSize through DataLayout (via ArrayType)
// agrees with the direct getSize path.
Container containers[2];

// CIR: cir.global external @containers = #cir.zero : !cir.array<!rec_Container x 2>
// LLVM: @containers = global [2 x %struct.Container] zeroinitializer
// OGCG: @containers = global [2 x %struct.Container] zeroinitializer

void use(Container *c);

void test() {
  Container c;
  c.type = 1;
  use(&c);
}
