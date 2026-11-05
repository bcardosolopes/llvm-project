// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c2x %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c2x %s -o - | FileCheck %s --check-prefix=OGCG

// A DIEnumerator's isUnsigned flag must reflect each enumerator value's own
// representation (matching classic CodeGen's DIBuilder::createEnumerator), not
// the enum's underlying integer type. A small positive enumerator is recorded
// as signed (no isUnsigned) even when the enum is backed by unsigned int; an
// enumerator that does not fit in a signed int is recorded as unsigned. The
// subprogram-path enum builder previously derived this from the underlying type,
// spuriously marking every enumerator of an unsigned-backed enum as isUnsigned.

typedef enum { TE_X = 100, TE_Y = 200 } TEnum;
enum Big { B_A = 3000000000u };

int use(TEnum t) { return (int)t; }
int useb(enum Big b) { return (int)b; }

// The small positive enumerators are signed: "value: 100)" with no isUnsigned.
// CHECK-DAG: !DIEnumerator(name: "TE_X", value: 100)
// CHECK-DAG: !DIEnumerator(name: "TE_Y", value: 200)
// The out-of-signed-range enumerator is unsigned.
// CHECK-DAG: !DIEnumerator(name: "B_A", value: 3000000000, isUnsigned: true)

// OGCG-DAG: !DIEnumerator(name: "TE_X", value: 100)
// OGCG-DAG: !DIEnumerator(name: "TE_Y", value: 200)
// OGCG-DAG: !DIEnumerator(name: "B_A", value: 3000000000, isUnsigned: true)
