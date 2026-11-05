// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -fsanitize=implicit-unsigned-integer-truncation -O0 -emit-llvm %s -o - | FileCheck %s --check-prefix=TRUNC
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -fsanitize=signed-integer-overflow -O0 -emit-llvm %s -o - | FileCheck %s --check-prefix=OVF

// When a UBSan check passes a wide value to its handler by address, it stores
// the value into a temp alloca. For a _BitInt whose value width (e.g. i100)
// differs from its in-memory storage width (i128), CIR stored the raw value
// into the storage-typed temp, mismatching the alloca's pointee type and failing
// CIR verification. The value must be converted to its memory representation
// first. Affects both the implicit-conversion and overflow check paths.

unsigned _BitInt(20) trunc_big(unsigned _BitInt(100) x) { return x; }
// TRUNC-LABEL: @trunc_big
// TRUNC: alloca i128
// TRUNC: store i128 %{{[0-9]+}}, ptr
// TRUNC: call void @__ubsan_handle_implicit_conversion

_BitInt(100) add_big(_BitInt(100) a, _BitInt(100) b) { return a + b; }
// OVF-LABEL: @add_big
// OVF: store i128 %{{[0-9]+}}, ptr
// OVF: call void @__ubsan_handle_add_overflow
