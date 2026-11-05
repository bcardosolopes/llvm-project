// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// Pointer increment should get nuw flag on GEP.
// Pointer decrement should NOT get nuw flag.

int *test_ptr_inc(int *p) { return ++p; }
// CIR-LABEL: @test_ptr_inc
// CIR: getelementptr inbounds nuw i32
// OGCG-LABEL: @test_ptr_inc
// OGCG: getelementptr inbounds nuw i32

int *test_ptr_dec(int *p) { return --p; }
// CIR-LABEL: @test_ptr_dec
// CIR: getelementptr inbounds i32, ptr %{{.*}}, i64 -1
// CIR-NOT: getelementptr inbounds nuw i32, ptr %{{.*}}, i64 -1
// OGCG-LABEL: @test_ptr_dec
// OGCG: getelementptr inbounds i32, ptr %{{.*}}, i32 -1

int *test_ptr_post_inc(int *p) { return p++; }
// CIR-LABEL: @test_ptr_post_inc
// CIR: getelementptr inbounds nuw i32
// OGCG-LABEL: @test_ptr_post_inc
// OGCG: getelementptr inbounds nuw i32
