//===--- ClangIRAnalysisWarnings.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The analysis-based warnings ClangIR can issue instead of Clang's CFG
// (-fclangir-analysis-warnings=).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_CLANGIRANALYSISWARNINGS_H
#define LLVM_CLANG_BASIC_CLANGIRANALYSISWARNINGS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include <bitset>
#include <optional>

namespace clang {

enum class ClangIRAnalysisWarning {
#define CLANGIR_ANALYSIS_WARNING(Name, Spelling) Name,
#include "clang/Basic/ClangIRAnalysisWarnings.def"
};

/// The warning spelled \p Value in -fclangir-analysis-warnings=, if any.
inline std::optional<ClangIRAnalysisWarning>
parseClangIRAnalysisWarning(llvm::StringRef Value) {
  return llvm::StringSwitch<std::optional<ClangIRAnalysisWarning>>(Value)
#define CLANGIR_ANALYSIS_WARNING(Name, Spelling)                               \
  .Case(Spelling, ClangIRAnalysisWarning::Name)
#include "clang/Basic/ClangIRAnalysisWarnings.def"
      .Default(std::nullopt);
}

/// A set of analysis-based warnings issued by ClangIR.
class ClangIRAnalysisWarningSet {
  static constexpr unsigned NumWarnings = 0
#define CLANGIR_ANALYSIS_WARNING(Name, Spelling) +1
#include "clang/Basic/ClangIRAnalysisWarnings.def"
      ;
  std::bitset<NumWarnings> Warnings;

public:
  void insert(ClangIRAnalysisWarning W) {
    Warnings.set(static_cast<unsigned>(W));
  }
  bool contains(ClangIRAnalysisWarning W) const {
    return Warnings.test(static_cast<unsigned>(W));
  }
};

} // namespace clang

#endif // LLVM_CLANG_BASIC_CLANGIRANALYSISWARNINGS_H
