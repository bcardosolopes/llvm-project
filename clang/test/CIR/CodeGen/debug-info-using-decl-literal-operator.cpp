// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG

// A using-declaration can name an entity whose DeclarationName is not a simple
// identifier -- here a literal operator, whose name is a CXXLiteralOperatorName.
// Resolving the imported entity through the by-name subprogram cache called
// NamedDecl::getName() unconditionally, which asserts on such a name. CIR now
// skips the imported declaration for a non-identifier name rather than emitting
// a nameless import of the enclosing namespace, which reads as
// 'using namespace n;'.

namespace n {
namespace lit {
inline int operator""_x(unsigned long long v) { return static_cast<int>(v); }
} // namespace lit
} // namespace n

using n::lit::operator""_x;

int f() { return 1_x; }

// CHECK-NOT: DIImportedEntity
// CHECK: distinct !DISubprogram(name: "f"
// CHECK-NOT: DIImportedEntity

// OGCG: !DIImportedEntity(tag: DW_TAG_imported_declaration
