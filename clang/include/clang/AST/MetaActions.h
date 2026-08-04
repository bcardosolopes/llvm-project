//===--- MetaActions.h - Interface for metafunction actions -----*- C++ -*-===//
//
// Copyright 2024 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Provides an interface for actions requiring semantic analysis from
///        C++26 reflection functions (i.e., metafunctions).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_METAACTIONS_H
#define LLVM_CLANG_AST_METAACTIONS_H

#include <clang/AST/TemplateBase.h>
#include <clang/AST/Type.h>
#include <clang/Basic/SourceLocation.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>

namespace clang {

class AttributeCommonInfo;
class ConceptDecl;
class CXXBasePath;
class CXXRecordDecl;
class Decl;
class DeclContext;
class DeclRefExpr;
class Expr;
class FunctionDecl;
class FunctionTemplateDecl;
class NamedDecl;
struct TagDataMemberSpec;
class TemplateDecl;
class TypeAliasTemplateDecl;
class VarDecl;
class VarTemplateDecl;

// Interface for actions requiring semantic analysis from C++26 reflection
// functions (i.e., metafunctions).
class MetaActions {
public:
  virtual ~MetaActions() {}

                            // ====================
                            // Access-Check Support
                            // ====================

  // Returns the current declaration context.
  virtual Decl *CurrentCtx() const = 0;

  // Returns whether the declaration 'D' is accessible from 'Ctx'.
  virtual bool IsAccessible(NamedDecl *Target, DeclContext *Ctx,
                            CXXRecordDecl *NamingCls) = 0;

  // Returns whether the base class 'B' is accessible from 'Ctx'.
  virtual bool IsAccessibleBase(QualType BaseTy, QualType DerivedTy,
                                const CXXBasePath &Path,
                                DeclContext *Ctx, SourceLocation AccessLoc) = 0;

                            // ====================
                            // Substitution Support
                            // ====================

  // Returns 'true' if 'TArgs' are allowed template arguments for 'TD'.
  // Otherwise, 'false'.
  virtual bool
  CheckTemplateArgumentList(TemplateDecl *TD,
                            SmallVectorImpl<TemplateArgument> &TArgs,
                            bool SuppressDiagnostics,
                            SourceLocation InstantiateLoc) = 0;

  // Returns the specialization 'TD<TArgs...>'. The template arguments are
  // assumed to be valid for the specialization, as a precondition.
  virtual QualType Substitute(TypeAliasTemplateDecl *TD,
                              ArrayRef<TemplateArgument> TArgs,
                              SourceLocation InstantiateLoc) = 0;
  virtual FunctionDecl *Substitute(FunctionTemplateDecl *TD,
                                   ArrayRef<TemplateArgument> TArgs,
                                   SourceLocation InstantiationLoc) = 0;
  virtual VarDecl *Substitute(VarTemplateDecl *TD,
                              ArrayRef<TemplateArgument> TArgs,
                              SourceLocation InstantiateLoc) = 0;
  virtual Expr *Substitute(ConceptDecl *TD, ArrayRef<TemplateArgument> TArgs,
                           SourceLocation InstantiateLoc) = 0;

                          // ========================
                          // Member Iteration Support
                          // ========================

  // If 'D' is a template specialization, then ensures that 'D' is instantiated.
  // Returns 'false' if 'D' could not be instantiated (e.g., failed
  // constraints). Otherwise, 'true'.
  virtual bool EnsureInstantiated(Decl *D, SourceRange Range) = 0;

  // Ensures that any implicit members of 'RD' have been declared.
  virtual void EnsureDeclarationOfImplicitMembers(CXXRecordDecl *RD) = 0;

  // P4340 ext: if 'T' is a class type with a user-provided reflect_constant
  // customization point, evaluates it on 'V' and returns the designated
  // variable via 'ResultVD' (checking the conditions on the returned object
  // and idempotence). If 'T' has no user-provided customization point,
  // returns 'false' with 'ResultVD' null after normalizing the subobjects of
  // 'V' in place. Returns 'true' on (diagnosed) error.
  virtual bool NormalizeReflectConstant(QualType T, APValue &V,
                                        VarDecl *&ResultVD,
                                        SourceLocation Loc) = 0;

  // Ensures instantiation of the exception specification of 'FD'.
  virtual void EnsureInstantiationOfExceptionSpec(SourceLocation Loc,
                                                  FunctionDecl *FD) = 0;

  // Returns 'true' if the constraints of 'FD' are satisfied.
  // Otherwise, 'false'.
  virtual bool HasSatisfiedConstraints(FunctionDecl *FD) = 0;

                             // ==================
                             // Invocation Support
                             // ==================

  // Returns the specialization of 'FD' deduced from the explicit template
  // arguments 'TArgs' and the function arguments 'Args'.
  virtual FunctionDecl *DeduceSpecialization(FunctionTemplateDecl *FTD,
                                             ArrayRef<TemplateArgument> TArgs,
                                             ArrayRef<Expr *> Args,
                                             SourceLocation InstantiateLoc) = 0;

  // Synthesizes a member-access expression for 'Obj.Mem', eliding member
  // lookup.
  virtual Expr *
  SynthesizeDirectMemberAccess(Expr *Obj, CXXReflectExpr *Mem,
                               SourceLocation PlaceholderLoc) = 0;

  // Synthesizes a call expression for 'Fn(Args...)'.
  virtual Expr *SynthesizeCallExpr(Expr *Fn, MutableArrayRef<Expr *> Args) = 0;

                           // =======================
                           // Class Synthesis Support
                           // =======================

  // Returns a new definition of 'D' having the members specified by 'Mems'.
  virtual
  CXXRecordDecl *DefineAggregate(CXXRecordDecl *IncompleteDecl,
                                 ArrayRef<TagDataMemberSpec *> MemberSpecs,
                                 Decl *ContainingDecl,
                                 SourceLocation DefinitionLoc) = 0;

  // Appertains the value represented by 'Value' as an annotation of 'Decl'.
  virtual CXX26AnnotationAttr *Annotate(Decl *TargetDecl, const APValue &Value,
                                        Decl *ContainingDecl,
                                        SourceLocation DefinitionLoc) = 0;

                        // ============================
                        // Annotation Synthesis Support
                        // ============================

  virtual AttributeCommonInfo *SynthesizeAnnotation(Expr *CE,
                                                    SourceLocation Loc) = 0;
};
} // namespace clang

#endif
