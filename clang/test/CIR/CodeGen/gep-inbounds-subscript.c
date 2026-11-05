// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// CIR omits `inbounds` on GEP for pointer subscript (p[i]) operations

// CIR-LABEL: @arr_get
// CIR: getelementptr inbounds i32, ptr
// OGCG-LABEL: @arr_get
// OGCG: getelementptr inbounds i32, ptr
int arr_get(int *p, int i) { return p[i]; }

// CIR-LABEL: @double_sub
// CIR: getelementptr inbounds double, ptr
// OGCG-LABEL: @double_sub
// OGCG: getelementptr inbounds double, ptr
double double_sub(double *p, int i) { return p[i]; }

// CIR-LABEL: @char_sub
// CIR: getelementptr inbounds i8, ptr
// OGCG-LABEL: @char_sub
// OGCG: getelementptr inbounds i8, ptr
char char_sub(char *p, int i) { return p[i]; }

// CIR-LABEL: @neg_idx
// CIR: getelementptr inbounds i32, ptr
// OGCG-LABEL: @neg_idx
// OGCG: getelementptr inbounds i32, ptr
int neg_idx(int *p) { return p[-1]; }

// Pointer addition should already have inbounds (this works correctly)
// CIR-LABEL: @ptr_add
// CIR: getelementptr inbounds i32, ptr
// OGCG-LABEL: @ptr_add
// OGCG: getelementptr inbounds i32, ptr
int *ptr_add(int *p, int n) { return p + n; }
