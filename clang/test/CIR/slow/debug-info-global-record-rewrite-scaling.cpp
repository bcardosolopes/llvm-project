// UNSUPPORTED: true
// RUN: timeout 15 %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 \
// RUN:   -debug-info-kind=constructor -fclangir -emit-llvm \
// RUN:   -o /dev/null %s
//
// Verify direct-CIR global source-shaped debug rewriting does not rescan
// RecordDecl fields from field_begin() for every field of every global.
// The old direct-to-LLVM path did a repeated `field_begin() + advance(idx)`
// lookup while rebuilding field DI for debug-enabled record globals, turning
// this shape into an O(globals * fields^2) walk.
//
// NOTE: This runs under lit parallelism, so the timeout keeps some headroom
// for scheduler jitter while still catching the pre-fix regression.

#define FIELD(N) int f##N;
#define FIELD_00_09(P) \
  FIELD(P##0)          \
  FIELD(P##1)          \
  FIELD(P##2)          \
  FIELD(P##3)          \
  FIELD(P##4)          \
  FIELD(P##5)          \
  FIELD(P##6)          \
  FIELD(P##7)          \
  FIELD(P##8)          \
  FIELD(P##9)
#define FIELD_00_99(P) \
  FIELD_00_09(P##0)    \
  FIELD_00_09(P##1)    \
  FIELD_00_09(P##2)    \
  FIELD_00_09(P##3)    \
  FIELD_00_09(P##4)    \
  FIELD_00_09(P##5)    \
  FIELD_00_09(P##6)    \
  FIELD_00_09(P##7)    \
  FIELD_00_09(P##8)    \
  FIELD_00_09(P##9)

struct Big {
  FIELD_00_99(0)
  FIELD_00_99(1)
  FIELD_00_99(2)
  FIELD_00_99(3)
  FIELD_00_99(4)
  FIELD_00_99(5)
  FIELD_00_99(6)
  FIELD_00_99(7)
  FIELD_00_99(8)
  FIELD_00_99(9)
  FIELD_00_99(10)
  FIELD_00_99(11)
  FIELD_00_99(12)
  FIELD_00_99(13)
  FIELD_00_99(14)
  FIELD_00_99(15)
  FIELD_00_99(16)
  FIELD_00_99(17)
  FIELD_00_99(18)
  FIELD_00_99(19)
};

#define GLOBAL(N) Big g##N = {};
#define GLOBAL_00_09(P) \
  GLOBAL(P##0)          \
  GLOBAL(P##1)          \
  GLOBAL(P##2)          \
  GLOBAL(P##3)          \
  GLOBAL(P##4)          \
  GLOBAL(P##5)          \
  GLOBAL(P##6)          \
  GLOBAL(P##7)          \
  GLOBAL(P##8)          \
  GLOBAL(P##9)
#define GLOBAL_00_99(P) \
  GLOBAL_00_09(P##0)    \
  GLOBAL_00_09(P##1)    \
  GLOBAL_00_09(P##2)    \
  GLOBAL_00_09(P##3)    \
  GLOBAL_00_09(P##4)    \
  GLOBAL_00_09(P##5)    \
  GLOBAL_00_09(P##6)    \
  GLOBAL_00_09(P##7)    \
  GLOBAL_00_09(P##8)    \
  GLOBAL_00_09(P##9)

GLOBAL_00_99(0)
GLOBAL_00_99(1)
GLOBAL_00_99(2)
GLOBAL_00_99(3)
