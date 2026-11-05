// RUN: clang-tblgen -gen-clang-attr-impl -I%p/../../include %s -o %t.1
// RUN: FileCheck %s --input-file %t.1
// RUN: clang-tblgen -gen-clang-attr-impl -I%p/../../include %s -o %t.2
// RUN: cmp %t.1 %t.2

include "clang/Basic/Attr.td"

// CHECK-LABEL: void AMDGPUMaxNumWorkGroupsAttr::printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const {
// CHECK: bool IsFirstArgument = true; (void)IsFirstArgument;
// CHECK: unsigned TrailingOmittedArgs = 0; (void)TrailingOmittedArgs;
// CHECK: case 0 : {
// CHECK: OS << "__attribute__((amdgpu_max_num_work_groups";
// CHECK: if (!getMaxNumWorkGroupsZ())
// CHECK-NEXT: ++TrailingOmittedArgs;
// CHECK: if (!getMaxNumWorkGroupsY())
// CHECK-NEXT: ++TrailingOmittedArgs;
// CHECK: DelimitAttributeArgument(OS, IsFirstArgument);
// CHECK: if (!(!getMaxNumWorkGroupsY())) {
// CHECK: if (!(!getMaxNumWorkGroupsZ())) {
