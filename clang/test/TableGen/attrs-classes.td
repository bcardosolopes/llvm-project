// RUN: clang-tblgen -gen-clang-attr-classes -I%p/../../include %s -o %t.1
// RUN: FileCheck %s --input-file %t.1
// RUN: clang-tblgen -gen-clang-attr-classes -I%p/../../include %s -o %t.2
// RUN: cmp %t.1 %t.2

include "clang/Basic/Attr.td"

// CHECK-LABEL: class CLANG_ABI ARMInterruptAttr : public InheritableAttr {
// CHECK: ARMInterruptAttr::InterruptType interrupt;
// CHECK: static ARMInterruptAttr *CreateImplicit(ASTContext &Ctx, ARMInterruptAttr::InterruptType Interrupt, const AttributeCommonInfo &CommonInfo);
// CHECK: static bool ConvertStrToInterruptType(StringRef Val, ARMInterruptAttr::InterruptType &Out);

// CHECK-LABEL: class CLANG_ABI MipsInterruptAttr : public InheritableAttr {
// CHECK: MipsInterruptAttr::InterruptType interrupt;
// CHECK: static bool ConvertStrToInterruptType(StringRef Val, MipsInterruptAttr::InterruptType &Out);

// CHECK-LABEL: class CLANG_ABI RISCVInterruptAttr : public InheritableAttr {
// CHECK: InterruptType *interrupt_;
// CHECK: static bool ConvertStrToInterruptType(StringRef Val, RISCVInterruptAttr::InterruptType &Out);
