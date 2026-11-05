// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The srcloc cookies are emitted again on the way out.

// CHECK-LABEL: define i32 @asm_srcloc
llvm.func @asm_srcloc(%arg0: i32) -> i32 {
  // CHECK: call i32 asm sideeffect
  // CHECK-SAME: !srcloc ![[SRC:[0-9]+]]
  %0 = llvm.inline_asm has_side_effects asm_dialect = att
      {srcloc = array<i64: 93, 187>} "mov $0, $1", "=r,r" %arg0 : (i32) -> i32
  llvm.return %0 : i32
}

// CHECK: ![[SRC]] = !{i64 93, i64 187}
