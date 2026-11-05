// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -o - %s | llvm-as -o /dev/null

// Verify that C++17 structured binding declarations produce DILocalVariable
// entries with correct names, types, and DW_OP_plus_uconst offset expressions,
// matching OGCG.

struct S { int x; };
struct U { S s; int n; };

void test_struct() {
  U u{};
  auto [s, n] = u;
}

void test_array() {
  int arr[3] = {1, 2, 3};
  auto [a, b, c] = arr;
}

// Struct binding 'n' uses DW_OP_plus_uconst 4 (sizeof(S) == 4).
// CHECK: #dbg_declare({{.*}}, !DIExpression(DW_OP_plus_uconst, 4)
// Array binding offsets.
// CHECK: #dbg_declare({{.*}}, !DIExpression(DW_OP_plus_uconst, 4)
// CHECK: #dbg_declare({{.*}}, !DIExpression(DW_OP_plus_uconst, 8)

// Binding types: 's' has struct S type, 'n' has int.
// CHECK-DAG: ![[INT:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DILocalVariable(name: "s", {{.*}}type: ![[S_BIND_TYPE:[0-9]+]])
// CHECK-DAG: ![[S_BIND_TYPE]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S",
// CHECK-DAG: !DILocalVariable(name: "n", {{.*}}type: ![[INT]])

// Array bindings all have int type.
// CHECK-DAG: !DILocalVariable(name: "a", {{.*}}type: ![[INT]])
// CHECK-DAG: !DILocalVariable(name: "b", {{.*}}type: ![[INT]])
// CHECK-DAG: !DILocalVariable(name: "c", {{.*}}type: ![[INT]])

// Enum binding: 'color' should have the enum type, not int.
enum class Color { Red, Green, Blue };
struct Pixel { Color c; int alpha; };

void test_enum() {
  Pixel p{Color::Red, 255};
  auto [color, alpha] = p;
}

// CHECK-DAG: !DILocalVariable(name: "color", {{.*}}type: ![[COLOR_TYPE:[0-9]+]])
// CHECK-DAG: ![[COLOR_TYPE]] = !DICompositeType(tag: DW_TAG_enumeration_type, name: "Color",
// CHECK-DAG: !DILocalVariable(name: "alpha", {{.*}}type: ![[INT]])
