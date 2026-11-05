// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

// An empty array asks for no idents, which is not the same as an empty
// `!llvm.ident` list. `getOrInsertNamedMetadata` creates the node whether or
// not anything goes into it, so the export has to decline to call it.

// CHECK-NOT: !llvm.ident
module attributes {llvm.ident = []} {
}
