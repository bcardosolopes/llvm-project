// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM

// Zero-sized fields from [[no_unique_address]] are not part of the CIR record
// layout. Accessing them should not crash in getCIRFieldNo — the address is
// computed from the ASTRecordLayout field offset instead.

struct Empty {};

struct Container {
    [[no_unique_address]] Empty e;
    int value;
};

// CIR-DAG: !rec_Container = !cir.record<struct "Container" {!s32i}>
// CIR-DAG: !rec_Empty = !cir.record<struct "Empty" padded {!u8i}>

// Test value access (non-zero-sized field in same struct).
int test_value() {
    Container c{};
    return c.value;
}

// CIR-LABEL: cir.func {{.*}}@_Z10test_valuev()
// CIR:         %[[C:.*]] = cir.alloca !rec_Container
// CIR:         cir.get_member %[[C]][0] {name = "value"} : !cir.ptr<!rec_Container> -> !cir.ptr<!s32i>
// CIR:       }

// Test zero-sized field access via member expression returning a reference.
Container& getRef();
Empty& test_zero_size_ref() {
    return getRef().e;
}

// The zero-sized field 'e' is at offset 0. CIR emits a bitcast from the
// container pointer to the empty struct pointer.
// CIR-LABEL: cir.func {{.*}}@_Z18test_zero_size_refv()
// CIR:         %[[REF:.*]] = cir.call @_Z6getRefv()
// CIR:         cir.cast bitcast %[[REF]] : !cir.ptr<!rec_Container> -> !cir.ptr<!rec_Empty>
// CIR:       }

// LLVM-LABEL: define {{.*}}noundef nonnull align 1 dereferenceable(1) ptr @_Z18test_zero_size_refv()
// LLVM:         %[[CALL:.*]] = call noundef nonnull align 4 dereferenceable(4) ptr @_Z6getRefv()
// LLVM:         ret ptr %[[CALL]]
// LLVM:       }

// Test direct zero-sized field access on a local variable.
Empty test_zero_size_local() {
    Container c{};
    return c.e;
}

// The empty field copy is a no-op (empty class), so no bitcast is emitted.
// CIR-LABEL: cir.func {{.*}}@_Z20test_zero_size_localv()
// CIR:         %[[C:.*]] = cir.alloca !rec_Container
// CIR:       }

// Test zero-sized field at non-zero offset and multiple zero-sized fields.
struct Multi {
    int a;
    [[no_unique_address]] Empty e1;
    int b;
    [[no_unique_address]] Empty e2;
};

Multi& getMulti();

// e2 is at offset 8 (after int a + int b, with e1 at offset 4).
// Requires i8 pointer stride + bitcast to field type.
Empty& test_nonzero_offset() {
    return getMulti().e2;
}

// CIR-LABEL: cir.func {{.*}}@_Z19test_nonzero_offsetv()
// CIR:         cir.ptr_stride
// CIR:         cir.cast bitcast {{.*}} -> !cir.ptr<!rec_Empty>
// CIR:       }

// LLVM-LABEL: define {{.*}}noundef nonnull align 1 dereferenceable(1) ptr @_Z19test_nonzero_offsetv()
// LLVM:         getelementptr inbounds i8, ptr %{{.*}}, i64
// LLVM:       }
