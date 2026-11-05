// Pointer-to-data-member offsets for a member of a polymorphic class must size
// the leading vptr with the target pointer width, not a hardcoded 64 bits.
// On 32-bit-pointer targets (i386/arm32 and 32-bit nvptx) the vptr is 4 bytes,
// so the first data member is at offset 4 (not 8) and the second at 8 (not 16).
// Regression test for the VPtrType DataLayout interface hardcoding an 8-byte
// vptr, which shifted every data-member-pointer offset in a polymorphic class on
// 32-bit-pointer targets (CPU i386/arm32 and CUDA device code on 32-bit nvptx).

// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=I386
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabi -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=ARM32
// RUN: %clang_cc1 -triple nvptx-nvidia-cuda -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=NV32
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=X64
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=ARM64
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=NV64

struct P {
  long x;
  long y;
  virtual void f();
};

long P::*get_x() { return &P::x; }
// I386-LABEL:  @_Z5get_xv
// I386:        ret i32 4
// ARM32-LABEL: @_Z5get_xv
// ARM32:       ret i32 4
// NV32-LABEL:  @_Z5get_xv
// NV32:        ret i32 4
// X64-LABEL:   @_Z5get_xv
// X64:         ret i64 8
// ARM64-LABEL: @_Z5get_xv
// ARM64:       ret i64 8
// NV64-LABEL:  @_Z5get_xv
// NV64:        ret i64 8

long P::*get_y() { return &P::y; }
// I386-LABEL:  @_Z5get_yv
// I386:        ret i32 8
// ARM32-LABEL: @_Z5get_yv
// ARM32:       ret i32 8
// NV32-LABEL:  @_Z5get_yv
// NV32:        ret i32 8
// X64-LABEL:   @_Z5get_yv
// X64:         ret i64 16
// ARM64-LABEL: @_Z5get_yv
// ARM64:       ret i64 16
// NV64-LABEL:  @_Z5get_yv
// NV64:        ret i64 16

// A pointer-to-data-member field (an Itanium ptrdiff_t) must itself be sized at
// the target pointer width, so a field after it in a polymorphic record gets the
// right offset. Q: vptr + (long P::*) + long -> `after` at 8 (i386/arm32) or 16.
struct Q {
  virtual void g();
  long P::*pm;
  long after;
};

long Q::*get_after() { return &Q::after; }
// I386-LABEL:  @_Z9get_afterv
// I386:        ret i32 8
// ARM32-LABEL: @_Z9get_afterv
// ARM32:       ret i32 8
// NV32-LABEL:  @_Z9get_afterv
// NV32:        ret i32 8
// X64-LABEL:   @_Z9get_afterv
// X64:         ret i64 16
// ARM64-LABEL: @_Z9get_afterv
// ARM64:       ret i64 16
// NV64-LABEL:  @_Z9get_afterv
// NV64:        ret i64 16
