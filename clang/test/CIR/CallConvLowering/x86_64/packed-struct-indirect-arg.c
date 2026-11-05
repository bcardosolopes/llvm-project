// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Packed struct {char, int, short} = 7 bytes with misaligned int at offset 1.
// The misaligned field causes the x86_64 ABI to classify this as Memory
// (Indirect), meaning it's passed by pointer with byval. This test covers
// three source patterns that produce different CIR instruction chains for
// loading the struct value before the call:
//   1. Direct variable access (LoadOp -> AllocaOp)
//   2. Array element access  (LoadOp -> GetElementOp)
//   3. Pointer dereference   (LoadOp -> LoadOp -> AllocaOp)

struct __attribute__((packed)) PackedS {
    char c;
    int i;
    short s;
};

int sum_packed(struct PackedS p);

// CIR-LABEL: @from_var()
// CIR:         call i32 @sum_packed(ptr noundef byval(%struct.PackedS) align 8
// OGCG-LABEL: @from_var()
// OGCG:        call i32 @sum_packed(ptr noundef byval(%struct.PackedS) align 8
int from_var(void) {
    struct PackedS ps = {1, 10, 100};
    return sum_packed(ps);
}

// CIR-LABEL: @from_array()
// CIR:         call i32 @sum_packed(ptr noundef byval(%struct.PackedS) align 8
// OGCG-LABEL: @from_array()
// OGCG:        call i32 @sum_packed(ptr noundef byval(%struct.PackedS) align 8
int from_array(void) {
    struct PackedS arr[1] = {{1, 10, 100}};
    return sum_packed(arr[0]);
}

// CIR-LABEL: @from_ptr()
// CIR:         call i32 @sum_packed(ptr noundef byval(%struct.PackedS) align 8
// OGCG-LABEL: @from_ptr()
// OGCG:        call i32 @sum_packed(ptr noundef byval(%struct.PackedS) align 8
int from_ptr(void) {
    struct PackedS ps = {1, 10, 100};
    struct PackedS *pp = &ps;
    return sum_packed(*pp);
}
