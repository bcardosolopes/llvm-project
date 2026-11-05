//===- Diagnostic.cpp - Dialect unit tests -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Threading.h"
#include "mlir/Support/TypeID.h"
#include "gtest/gtest.h"

#include <numeric>

using namespace mlir;
using namespace mlir::detail;

namespace {

TEST(DiagnosticLifetime, TestCopiesConstCharStar) {
  const auto *expectedMessage = "Error 1, don't mutate this";

  // Copy expected message into a mutable container, and call the constructor.
  std::string myStr(expectedMessage);

  mlir::MLIRContext context;
  Diagnostic diagnostic(mlir::UnknownLoc::get(&context),
                        DiagnosticSeverity::Note);
  diagnostic << myStr.c_str();

  // Mutate underlying pointer, but ensure diagnostic still has orig. message
  myStr[0] = '^';

  std::string resultMessage;
  llvm::raw_string_ostream stringStream(resultMessage);
  diagnostic.print(stringStream);
  ASSERT_STREQ(expectedMessage, resultMessage.c_str());
}

TEST(DiagnosticLifetime, TestLazyCopyStringLiteral) {
  char charArr[21] = "Error 1, mutate this";
  mlir::MLIRContext context;
  Diagnostic diagnostic(mlir::UnknownLoc::get(&context),
                        DiagnosticSeverity::Note);

  // Diagnostic contains optimization which assumes string literals are
  // represented by `const char[]` type. This is imperfect as we can sometimes
  // trick the type system as seen below.
  //
  // Still we use this to check the diagnostic is lazily storing the pointer.
  auto addToDiagnosticAsConst = [&diagnostic](const char(&charArr)[21]) {
    diagnostic << charArr;
  };
  addToDiagnosticAsConst(charArr);

  // Mutate the underlying pointer and ensure the string does change
  charArr[0] = '^';

  std::string resultMessage;
  llvm::raw_string_ostream stringStream(resultMessage);
  diagnostic.print(stringStream);
  ASSERT_STREQ("^rror 1, mutate this", resultMessage.c_str());
}

TEST(ParallelDiagnostics, NestedLoopsKeepElementOrder) {
  // An element of the outer loop emits a diagnostic, runs an inner loop whose
  // elements emit some, and emits another. A thread waiting for its inner loop
  // can run other elements of either loop meanwhile; the diagnostics still
  // come out in element order.
  MLIRContext context;
  std::vector<std::string> messages;
  ScopedDiagnosticHandler handler(&context, [&](Diagnostic &diag) {
    messages.push_back(diag.str());
  });
  constexpr size_t numOuter = 64, numInner = 16;
  std::vector<size_t> outer(numOuter);
  std::iota(outer.begin(), outer.end(), 0);
  parallelForEach(&context, outer, [&](size_t i) {
    Location loc = UnknownLoc::get(&context);
    emitError(loc) << "before " << i;
    std::vector<size_t> inner(numInner);
    std::iota(inner.begin(), inner.end(), 0);
    parallelForEach(&context, inner, [&](size_t j) {
      emitError(loc) << "inner " << i << " " << j;
    });
    emitError(loc) << "after " << i;
  });

  std::vector<std::string> expected;
  for (size_t i = 0; i < numOuter; ++i) {
    expected.push_back("before " + std::to_string(i));
    for (size_t j = 0; j < numInner; ++j)
      expected.push_back("inner " + std::to_string(i) + " " +
                         std::to_string(j));
    expected.push_back("after " + std::to_string(i));
  }
  EXPECT_EQ(messages, expected);
}

TEST(ParallelDiagnostics, ElementRunWhileNestedGoesToItsOwnLoop) {
  // Replays on one thread a worker that, waiting for a third-level loop inside
  // element 0 of a middle loop inside element 1 of an outer loop, runs element
  // 2 of the outer loop. That element's diagnostic belongs to the outer loop,
  // after all of element 1's.
  MLIRContext context;
  std::vector<std::string> messages;
  ScopedDiagnosticHandler handler(&context, [&](Diagnostic &diag) {
    messages.push_back(diag.str());
  });
  Location loc = UnknownLoc::get(&context);
  {
    ParallelDiagnosticHandler outer(&context);
    outer.setOrderIDForThread(1);
    emitError(loc) << "outer 1 before";
    {
      ParallelDiagnosticHandler middle(&context);
      middle.setOrderIDForThread(0);
      {
        ParallelDiagnosticHandler inner(&context);
        outer.setOrderIDForThread(2);
        emitError(loc) << "outer 2";
        outer.eraseOrderIDForThread();
      }
      emitError(loc) << "middle 0";
      middle.eraseOrderIDForThread();
    }
    emitError(loc) << "outer 1 after";
    outer.eraseOrderIDForThread();
  }
  EXPECT_EQ(messages, (std::vector<std::string>{"outer 1 before", "middle 0",
                                                "outer 1 after", "outer 2"}));
}

} // namespace
