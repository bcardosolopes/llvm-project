// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// For large homogeneous byte arrays, CIR should store the initializer as
// StringAttr-backed #cir.const_array<"..."> instead of a giant ArrayAttr of
// IntAttr entries.

unsigned char big_homogeneous[2048] = {[0 ... 2047] = 0xAB};

// CHECK: @big_homogeneous = #cir.const_array<"
// CHECK: !cir.array<!u8i x 2048>
// CHECK-NOT: @big_homogeneous = #cir.const_array<[
