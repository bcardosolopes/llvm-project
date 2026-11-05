// REQUIRES: x86-registered-target
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +amx-tile \
// RUN:   -target-feature +amx-int8 -target-feature +amx-bf16 -fclangir \
// RUN:   -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +amx-tile \
// RUN:   -target-feature +amx-int8 -target-feature +amx-bf16 \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// The x86_amx-typed AMX ".internal" intrinsics use the LLVM x86_amx type,
// which is represented in CIR as an opaque type tagged "x86.amx" and lowered by
// the x86 CIR->LLVM type converter to LLVM::LLVMX86AMXType. ClangIR now emits
// the same x86_amx-typed intrinsics as classic CodeGen, bridging the C-level
// v256i32 tile vectors to/from x86_amx with the cast.vector.to.tile /
// cast.tile.to.vector intrinsics.

typedef int _tile1024i __attribute__((__vector_size__(1024), __aligned__(64)));

// tileloadd64: returns x86_amx, cast back to the v256i32 tile vector.
_tile1024i test_tileloadd64(unsigned short m, unsigned short n, const void *p,
                            long s) {
  return __builtin_ia32_tileloadd64_internal(m, n, p, s);
}
// CIR-LABEL: define{{.*}} <256 x i32> @test_tileloadd64
// CIR: %[[T:.*]] = call x86_amx @llvm.x86.tileloadd64.internal(i16 %{{.*}}, i16 %{{.*}}, ptr %{{.*}}, i64 %{{.*}})
// CIR: call <256 x i32> @llvm.x86.cast.tile.to.vector.v256i32(x86_amx %[[T]])
// OGCG-LABEL: define{{.*}} <256 x i32> @test_tileloadd64
// OGCG: %[[OT:.*]] = call x86_amx @llvm.x86.tileloadd64.internal(i16 %{{.*}}, i16 %{{.*}}, ptr %{{.*}}, i64 %{{.*}})
// OGCG: call <256 x i32> @llvm.x86.cast.tile.to.vector.v256i32(x86_amx %[[OT]])

// tilestored64: cast the v256i32 tile vector to x86_amx before the store.
void test_tilestored64(unsigned short m, unsigned short n, void *p, long s,
                       _tile1024i t) {
  __builtin_ia32_tilestored64_internal(m, n, p, s, t);
}
// CIR-LABEL: define{{.*}} void @test_tilestored64
// CIR: %[[V:.*]] = call x86_amx @llvm.x86.cast.vector.to.tile.v256i32(<256 x i32> %{{.*}})
// CIR: call void @llvm.x86.tilestored64.internal(i16 %{{.*}}, i16 %{{.*}}, ptr %{{.*}}, i64 %{{.*}}, x86_amx %[[V]])
// OGCG-LABEL: define{{.*}} void @test_tilestored64
// OGCG: %[[OV:.*]] = call x86_amx @llvm.x86.cast.vector.to.tile.v256i32(<256 x i32> %{{.*}})
// OGCG: call void @llvm.x86.tilestored64.internal(i16 %{{.*}}, i16 %{{.*}}, ptr %{{.*}}, i64 %{{.*}}, x86_amx %[[OV]])

// tilezero: returns x86_amx, cast back to the v256i32 tile vector.
_tile1024i test_tilezero(unsigned short m, unsigned short n) {
  return __builtin_ia32_tilezero_internal(m, n);
}
// CIR-LABEL: define{{.*}} <256 x i32> @test_tilezero
// CIR: %[[Z:.*]] = call x86_amx @llvm.x86.tilezero.internal(i16 %{{.*}}, i16 %{{.*}})
// CIR: call <256 x i32> @llvm.x86.cast.tile.to.vector.v256i32(x86_amx %[[Z]])
// OGCG-LABEL: define{{.*}} <256 x i32> @test_tilezero
// OGCG: call x86_amx @llvm.x86.tilezero.internal
// OGCG: call <256 x i32> @llvm.x86.cast.tile.to.vector.v256i32

// tdpbssd: three x86_amx inputs (each cast from a v256i32 tile vector) and one
// x86_amx result (cast back).
_tile1024i test_tdpbssd(unsigned short m, unsigned short n, unsigned short k,
                        _tile1024i d, _tile1024i a, _tile1024i b) {
  return __builtin_ia32_tdpbssd_internal(m, n, k, d, a, b);
}
// CIR-LABEL: define{{.*}} <256 x i32> @test_tdpbssd
// CIR: call x86_amx @llvm.x86.cast.vector.to.tile.v256i32(<256 x i32> %{{.*}})
// CIR: call x86_amx @llvm.x86.cast.vector.to.tile.v256i32(<256 x i32> %{{.*}})
// CIR: call x86_amx @llvm.x86.cast.vector.to.tile.v256i32(<256 x i32> %{{.*}})
// CIR: %[[R:.*]] = call x86_amx @llvm.x86.tdpbssd.internal(i16 %{{.*}}, i16 %{{.*}}, i16 %{{.*}}, x86_amx %{{.*}}, x86_amx %{{.*}}, x86_amx %{{.*}})
// CIR: call <256 x i32> @llvm.x86.cast.tile.to.vector.v256i32(x86_amx %[[R]])
// OGCG-LABEL: define{{.*}} <256 x i32> @test_tdpbssd
// OGCG: call x86_amx @llvm.x86.tdpbssd.internal(i16 %{{.*}}, i16 %{{.*}}, i16 %{{.*}}, x86_amx %{{.*}}, x86_amx %{{.*}}, x86_amx %{{.*}})

// tdpbf16ps: same shape as tdpbssd, exercising the amx-bf16 feature.
_tile1024i test_tdpbf16ps(unsigned short m, unsigned short n, unsigned short k,
                          _tile1024i d, _tile1024i a, _tile1024i b) {
  return __builtin_ia32_tdpbf16ps_internal(m, n, k, d, a, b);
}
// CIR-LABEL: define{{.*}} <256 x i32> @test_tdpbf16ps
// CIR: call x86_amx @llvm.x86.tdpbf16ps.internal(i16 %{{.*}}, i16 %{{.*}}, i16 %{{.*}}, x86_amx %{{.*}}, x86_amx %{{.*}}, x86_amx %{{.*}})
// OGCG-LABEL: define{{.*}} <256 x i32> @test_tdpbf16ps
// OGCG: call x86_amx @llvm.x86.tdpbf16ps.internal(i16 %{{.*}}, i16 %{{.*}}, i16 %{{.*}}, x86_amx %{{.*}}, x86_amx %{{.*}}, x86_amx %{{.*}})
