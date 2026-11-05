// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple i686-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.i686.ll
// RUN: FileCheck --input-file=%t.i686.ll %s --check-prefix=I686
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fexperimental-max-bitint-width=1024 -fclangir -emit-llvm %s -o %t.a64.ll
// RUN: FileCheck --input-file=%t.a64.ll %s --check-prefix=A64
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fexperimental-max-bitint-width=1024 -emit-llvm %s -o %t.a64.ogcg.ll
// RUN: FileCheck --input-file=%t.a64.ogcg.ll %s --check-prefix=A64-OGCG
//
// Test that large _BitInt types use byte array [N x i8] representation for
// byval/sret attributes, matching OGCG's typeRequiresSplitIntoByteArray.
// This is needed because LLVM's alignment for iN may produce a different alloc
// size than the AST expects (e.g., i129 has LLVM alloc 32 bytes but AST
// expects 24 bytes).

// _BitInt(129): 24 bytes (AST), 32 bytes (LLVM alloc of i129) — need [24 x i8]
_BitInt(129) return_129(void) { return 0; }
void take_129(_BitInt(129) x) {}
void test_129(void) {
  _BitInt(129) x = return_129();
  take_129(x);
}

// I686-LABEL: @return_129(
// I686:         %{{.+}} = alloca [20 x i8], i64 1, align 4
// I686:         store i160 0, ptr %{{.+}}, align 4
// I686:         %{{.+}} = load i160, ptr %{{.+}}, align 4
// I686:         %{{.+}} = trunc i160 %{{.+}} to i129
// I686:         %{{.+}} = sext i129 %{{.+}} to i160
// I686:         store i160 %{{.+}}, ptr %0, align 4
//
// I686-LABEL: @take_129(
// I686:         %{{.+}} = alloca [20 x i8], i64 1, align 4
// I686:         %{{.+}} = load i160, ptr %0, align 4
// I686:         %{{.+}} = trunc i160 %{{.+}} to i129
// I686:         %{{.+}} = sext i129 %{{.+}} to i160
// I686:         store i160 %{{.+}}, ptr %{{.+}}, align 4
//
// I686-LABEL: @test_129(
// I686:         call void @return_129(ptr {{.*}} sret([20 x i8]) align 4
// I686:         %{{.+}} = load i160, ptr %{{.+}}, align 4
// I686:         %{{.+}} = trunc i160 %{{.+}} to i129
// I686:         %{{.+}} = sext i129 %{{.+}} to i160
// I686:         store i160 %{{.+}}, ptr %{{.+}}, align 4
// I686:         %{{.+}} = load i160, ptr %{{.+}}, align 4
// I686:         %{{.+}} = trunc i160 %{{.+}} to i129
// I686:         call void @take_129(ptr {{.*}} noundef %{{.+}})
//
// LLVM-LABEL: @test_129
// LLVM:         call void @return_129(ptr {{.*}} sret([24 x i8]) align 8
// LLVM:         call void @take_129(ptr {{.*}} byval([24 x i8]) align 8

// OGCG-LABEL: @test_129
// OGCG:         call void @return_129(ptr {{.*}} sret([24 x i8]) align 8
// OGCG:         call void @take_129(ptr {{.*}} byval([24 x i8]) align 8

// A64-LABEL: @return_129(
// A64:         store i256 0, ptr %{{.+}}, align 16
// A64:         %{{.+}} = load i256, ptr %{{.+}}, align 16
// A64:         store i256 %{{.+}}, ptr %0, align 16
//
// A64-LABEL: @take_129(
// A64:         %{{.+}} = load i256, ptr %0, align 16
// A64:         store i256 %{{.+}}, ptr %{{.+}}, align 16
//
// A64-LABEL: @test_129(
// A64:         %{{.+}} = alloca i256, i64 1, align 16
// A64:         call void @return_129(ptr {{.*}} sret(i256) align 16
// A64:         call void @take_129(ptr {{.*}} noundef
//
// A64-OGCG-LABEL: @test_129(
// A64-OGCG:         call void @return_129(ptr {{.*}} sret(i256) align 16
// A64-OGCG:         call void @take_129(ptr {{.*}} noundef

// _BitInt(257): 40 bytes (AST), 48 bytes (LLVM alloc of i257) — need [40 x i8]
_BitInt(257) return_257(void) { return 0; }
void take_257(_BitInt(257) x) {}
void test_257(void) {
  _BitInt(257) x = return_257();
  take_257(x);
}

// I686-LABEL: @return_257(
// I686:         %{{.+}} = alloca [36 x i8], i64 1, align 4
// I686:         store i288 0, ptr %{{.+}}, align 4
// I686:         %{{.+}} = load i288, ptr %{{.+}}, align 4
// I686:         %{{.+}} = trunc i288 %{{.+}} to i257
// I686:         %{{.+}} = sext i257 %{{.+}} to i288
// I686:         store i288 %{{.+}}, ptr %0, align 4
//
// I686-LABEL: @take_257(
// I686:         %{{.+}} = alloca [36 x i8], i64 1, align 4
// I686:         %{{.+}} = load i288, ptr %0, align 4
// I686:         %{{.+}} = trunc i288 %{{.+}} to i257
// I686:         %{{.+}} = sext i257 %{{.+}} to i288
// I686:         store i288 %{{.+}}, ptr %{{.+}}, align 4
//
// I686-LABEL: @test_257(
// I686:         call void @return_257(ptr {{.*}} sret([36 x i8]) align 4
// I686:         %{{.+}} = load i288, ptr %{{.+}}, align 4
// I686:         %{{.+}} = trunc i288 %{{.+}} to i257
// I686:         %{{.+}} = sext i257 %{{.+}} to i288
// I686:         store i288 %{{.+}}, ptr %{{.+}}, align 4
// I686:         %{{.+}} = load i288, ptr %{{.+}}, align 4
// I686:         %{{.+}} = trunc i288 %{{.+}} to i257
// I686:         call void @take_257(ptr {{.*}} noundef %{{.+}})
//
// LLVM-LABEL: @test_257
// LLVM:         call void @return_257(ptr {{.*}} sret([40 x i8]) align 8
// LLVM:         call void @take_257(ptr {{.*}} byval([40 x i8]) align 8

// OGCG-LABEL: @test_257
// OGCG:         call void @return_257(ptr {{.*}} sret([40 x i8]) align 8
// OGCG:         call void @take_257(ptr {{.*}} byval([40 x i8]) align 8

// A64-LABEL: @return_257(
// A64:         store i384 0, ptr %{{.+}}, align 16
// A64:         %{{.+}} = load i384, ptr %{{.+}}, align 16
// A64:         store i384 %{{.+}}, ptr %0, align 16
//
// A64-LABEL: @take_257(
// A64:         %{{.+}} = load i384, ptr %0, align 16
// A64:         store i384 %{{.+}}, ptr %{{.+}}, align 16
//
// A64-LABEL: @test_257(
// A64:         %{{.+}} = alloca i384, i64 1, align 16
// A64:         call void @return_257(ptr {{.*}} sret(i384) align 16
// A64:         call void @take_257(ptr {{.*}} noundef
//
// A64-OGCG-LABEL: @test_257(
// A64-OGCG:         call void @return_257(ptr {{.*}} sret(i384) align 16
// A64-OGCG:         call void @take_257(ptr {{.*}} noundef

// _BitInt(128): 16 bytes — no byval/sret needed, passed in registers.
_BitInt(128) return_128(void);

// LLVM: define dso_local i128 @test_128
// OGCG: define dso_local i128 @test_128
_BitInt(128) test_128(void) { return return_128(); }
