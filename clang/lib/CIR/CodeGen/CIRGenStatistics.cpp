#include "CIRGenStatistics.h"

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::CIRGen;

// TODO: Make this a front-end option. There is no easy way to get the
// FrontEndOptions at this point of the program without making rebasing more
// difficult.
static llvm::cl::opt<bool> printArrayInitsOpt("print-array-inits");

CIRGenStatistics CIRGenStatistics::Stats;

// TODO: Implement destructor that prints results.
CIRGenStatistics::~CIRGenStatistics() {

  // Print the array inits.
  if (isPrintArrayInitsEnabled()) {
    llvm::dbgs() << "{ \"inits\": [ ";
    bool first = true;
    for (auto &init : ArrayInits) {
      if (first)
        first = false;
      else
        llvm::dbgs() << ", ";

      llvm::dbgs() << init;
    }
    llvm::dbgs() << "] }\n";
  }
}

bool CIRGenStatistics::isPrintArrayInitsEnabled() {
  return printArrayInitsOpt.getValue();
}

void CIRGenStatistics::recordArrayInit(mlir::Attribute attrs,
                                       cir::ArrayType arrayTy) {
  if (isPrintArrayInitsEnabled()) {

    // Start a new JSON object in the output.
    llvm::raw_string_ostream json(ArrayInits.emplace_back());

    // Get the type as a string, and format it so JSON doesn't get mad.
    std::string typeStr;
    llvm::raw_string_ostream typeStream(typeStr);
    typeStream << arrayTy;
    std::replace(typeStr.begin(), typeStr.end(), '\"', '\'');

    // Emit type information.
    int size = arrayTy.getSize();
    json << "{ \"type\": \"" << typeStr << "\"" // array type
         << ", \"size\": " << size;             // size of array

    // Emit elements information.
    int initSize = 0, numZeros = 0, numConstants = 0, numVars = 0;
    if (auto arrayAttr = dyn_cast<mlir::ArrayAttr>(attrs)) {
      initSize = arrayAttr.size();

      json << ", \"elements\": [ ";
      bool first = true;
      for (auto attr : arrayAttr) {
        ++numConstants;

        if (first)
          first = false;
        else
          json << ", ";

        if (auto intAttr = mlir::dyn_cast<cir::IntAttr>(attr)) {
          if (intAttr.getUInt() == 0)
            ++numZeros;
          json << intAttr.getUInt();
        } else if (auto fpAttr = mlir::dyn_cast<cir::FPAttr>(attr)) {
          if (fpAttr.getValue().isPosZero())
            ++numZeros;
          json << fpAttr.getValue();
        } else {
          json << "\"?\"";
        }
      }
      json << "]";
    } else if (auto strAttr = dyn_cast<mlir::StringAttr>(attrs)) {
      initSize = strAttr.size();

      // NOTE: We don't emit the string because possible escape characters cause
      // issues with JSON parsing.
    }

    // Get the number of trailing zeroes.
    int trail = size - initSize;
    numZeros += trail;

    // Emit element summary.
    json << ", \"initsize\": " << initSize      // size of init list
         << ", \"zeroes\": " << numZeros        // # of zeros in init list
         << ", \"constants\": " << numConstants // # of constants in init list
         << ", \"variables\": " << numVars      // # of variables in init list
         << ", \"trail\": " << trail            // # of trailing zeros
         << " }";                               // END OF JSON OBJECT
  }
}

void CIRGenStatistics::recordArrayInit(llvm::ArrayRef<mlir::Value> InitList,
                                       cir::ArrayType AType) {
  if (isPrintArrayInitsEnabled()) {

    // Get the array size.
    int size = AType.getSize();
    int initSize = InitList.size();

    // Format the type as a string.
    std::string typeStr;
    llvm::raw_string_ostream typeStream(typeStr);
    typeStream << AType;
    std::replace(typeStr.begin(), typeStr.end(), '\"', '\'');

    // Emit the JSON object.
    llvm::raw_string_ostream json(ArrayInits.emplace_back());
    json << "{ \"type\": \"" << typeStr << "\"" // array type
         << ", \"size\": " << size              // size of array
         << ", \"initsize\": " << initSize;     // size of init list

    // Get the number of trailing zeroes.
    int trail = size - initSize;

    // Characterize the initializer list.
    int numZeros = trail, numConstants = 0, numVars = 0;
    json << ", \"elements\": [ ";
    bool first = true;
    for (auto val : InitList) {
      if (first)
        first = false;
      else
        json << ", ";

      if (auto constOp = mlir::dyn_cast<cir::ConstantOp>(val.getDefiningOp())) {
        ++numConstants;
        if (mlir::isa<cir::IntType>(constOp.getType())) {
          auto constVal = mlir::cast<cir::IntAttr>(constOp.getValue());
          if (constVal.getUInt() == 0)
            ++numZeros;
          json << constVal.getUInt();
        } else if (auto fpAttr =
                       mlir::dyn_cast<cir::FPAttr>(constOp.getValue())) {
          if (fpAttr.getValue().isPosZero())
            ++numZeros;
          json << fpAttr.getValue();
        } else {
          json << "\"?\"";
        }
      } else {
        json << "\"VAR\"";
        ++numVars;
      }
    }
    json << "]";

    // Emit element summary.
    json << ", \"zeroes\": " << numZeros        // # of zeros in init list
         << ", \"constants\": " << numConstants // # of constants in init list
         << ", \"variables\": " << numVars      // # of variables in init list
         << ", \"trail\": " << trail            // # of trailing zeros
         << " }";                               // END OF JSON OBJECT
  }
}

void CIRGenStatistics::collectArrayInit(
    llvm::SmallVector<mlir::Value> &InitList, Address Addr) {
  if (isPrintArrayInitsEnabled()) {
    // Find what was stored to the lvalue by checking its uses.
    if (mlir::Operation *addrDef = Addr.getDefiningOp()) {
      for (mlir::Operation *user : addrDef->getUsers()) {
        if (auto store = mlir::dyn_cast<cir::StoreOp>(user)) {
          InitList.push_back(store.getValue());
          break;
        }
      }
    }
  }
}
