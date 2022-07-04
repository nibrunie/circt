//===- SSPOps.cpp - SSP operation implementation --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SSP (static scheduling problem) dialect operations.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/SSP/SSPOps.h"
#include "circt/Dialect/SSP/SSPAttributes.h"
#include "circt/Support/LLVM.h"

#include "mlir/IR/Builders.h"

using namespace circt;
using namespace circt::ssp;

//===----------------------------------------------------------------------===//
// OperationOp
//===----------------------------------------------------------------------===//

ParseResult OperationOp::parse(OpAsmParser &parser, OperationState &result) {
  auto &builder = parser.getBuilder();

  // (Scheduling) operation's name
  StringAttr opName;
  (void)parser.parseOptionalSymbolName(opName, SymbolTable::getSymbolAttrName(),
                                       result.attributes);

  // Dependences
  SmallVector<OpAsmParser::UnresolvedOperand> unresolvedOperands;
  SmallVector<Attribute> dependences;
  unsigned operandIdx = 0;
  auto parseDependenceSourceWithAttrDict = [&]() -> ParseResult {
    llvm::SMLoc loc = parser.getCurrentLocation();
    FlatSymbolRefAttr sourceRef;
    ArrayAttr properties;

    // Try to parse a symbol reference first...
    if (!parser.parseOptionalAttribute(sourceRef).hasValue()) {
      // ...and if that fails, attempt to parse an SSA operand.
      OpAsmParser::UnresolvedOperand operand;
      if (failed(parser.parseOperand(operand)))
        return parser.emitError(loc, "expected SSA value or symbol reference");

      unresolvedOperands.push_back(operand);
    }

    // Parse the properties, if present.
    parser.parseOptionalAttribute(properties);

    // No need to explicitly store SSA deps without properties.
    if (sourceRef || properties)
      dependences.push_back(
          builder.getAttr<DependenceAttr>(operandIdx, sourceRef, properties));

    ++operandIdx;
    return success();
  };

  if (parser.parseCommaSeparatedList(AsmParser::Delimiter::Paren,
                                     parseDependenceSourceWithAttrDict))
    return failure();

  if (!dependences.empty())
    result.addAttribute(builder.getStringAttr("dependences"),
                        builder.getArrayAttr(dependences));

  // Properties
  ArrayAttr properties;
  parser.parseOptionalAttribute(properties);
  if (properties)
    result.addAttribute(builder.getStringAttr("properties"), properties);

  // Parse default attr-dict
  (void)parser.parseOptionalAttrDict(result.attributes);

  // Resolve operands
  SmallVector<Value> operands;
  if (parser.resolveOperands(unresolvedOperands, builder.getNoneType(),
                             operands))
    return failure();
  result.addOperands(operands);

  // Mockup results
  SmallVector<Type> types(parser.getNumResults(), builder.getNoneType());
  result.addTypes(types);

  return success();
}

void OperationOp::print(OpAsmPrinter &p) {
  // (Scheduling) operation's name
  if (StringAttr symName = getSymNameAttr()) {
    p << ' ';
    p.printSymbolName(symName);
  }

  // Dependences = SSA operands + other OperationOps via symbol references.
  // Emitted format looks like this:
  // (%0, %1 [#ssp.some_property<42>, ...], %2, ...,
  //  @op0, @op1 [#ssp.some_property<17>, ...], ...)
  SmallVector<DependenceAttr> defUseDeps(getNumOperands()), auxDeps;
  if (ArrayAttr dependences = getDependencesAttr()) {
    for (auto dep : dependences.getAsRange<DependenceAttr>()) {
      if (dep.getSourceRef())
        auxDeps.push_back(dep);
      else
        defUseDeps[dep.getOperandIdx()] = dep;
    }
  }

  p << '(';
  llvm::interleaveComma((*this)->getOpOperands(), p, [&](OpOperand &operand) {
    p.printOperand(operand.get());
    if (DependenceAttr dep = defUseDeps[operand.getOperandNumber()]) {
      p << ' ';
      p.printAttribute(dep.getProperties());
    }
  });
  if (!auxDeps.empty()) {
    if (!defUseDeps.empty())
      p << ", ";
    llvm::interleaveComma(auxDeps, p, [&](DependenceAttr dep) {
      p.printAttribute(dep.getSourceRef());
      if (ArrayAttr depProps = dep.getProperties()) {
        p << ' ';
        p.printAttribute(depProps);
      }
    });
  }
  p << ')';

  // Properties
  if (ArrayAttr properties = getPropertiesAttr()) {
    p << ' ';
    p.printAttribute(properties);
  }

  // Default attr-dict
  SmallVector<StringRef> elidedAttrs = {
      SymbolTable::getSymbolAttrName(),
      OperationOp::getDependencesAttrName().getValue(),
      OperationOp::getPropertiesAttrName().getValue()};
  p.printOptionalAttrDict((*this)->getAttrs(), elidedAttrs);
}

LogicalResult OperationOp::verify() {
  ArrayAttr dependences = getDependencesAttr();
  if (!dependences)
    return success();

  int nOperands = getNumOperands();
  int lastIdx = -1;
  for (auto dep : dependences.getAsRange<DependenceAttr>()) {
    int idx = dep.getOperandIdx();
    FlatSymbolRefAttr sourceRef = dep.getSourceRef();

    if (!sourceRef) {
      // Def-use deps use the index to refer to one of the SSA operands.
      if (idx >= nOperands)
        return emitError(
            "Operand index is out of bounds for def-use dependence attribute");

      // Indices may be sparse, but shall be sorted and unique.
      if (idx <= lastIdx)
        return emitError("Def-use operand indices in dependence attribute are "
                         "not monotonically increasing");
    } else {
      // Auxiliary deps are expected to follow the def-use deps (if present),
      // and hence use indices >= #operands.
      if (idx < nOperands)
        return emitError() << "Auxiliary dependence from " << sourceRef
                           << " is interleaved with SSA operands";

      // Indices shall be consecutive (special case: the first aux dep)
      if (!((idx == lastIdx + 1) || (idx > lastIdx && idx == nOperands)))
        return emitError("Auxiliary operand indices in dependence attribute "
                         "are not consecutive");
    }

    lastIdx = idx;
  }
  return success();
}

LogicalResult
OperationOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  ArrayAttr dependences = getDependencesAttr();
  if (!dependences)
    return success();

  for (auto dep : dependences.getAsRange<DependenceAttr>()) {
    FlatSymbolRefAttr sourceRef = dep.getSourceRef();
    if (!sourceRef)
      continue;

    Operation *sourceOp = symbolTable.lookupNearestSymbolFrom(*this, sourceRef);
    if (!sourceOp || !isa<OperationOp>(sourceOp))
      return emitOpError("references invalid source operation: ") << sourceRef;
  }

  return success();
}

//===----------------------------------------------------------------------===//
// TableGen'ed code
//===----------------------------------------------------------------------===//

#define GET_OP_CLASSES
#include "circt/Dialect/SSP/SSP.cpp.inc"