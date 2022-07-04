//===- Utilities.h - SSP <-> circt::scheduling infra conversion -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides utilities for the conversion between SSP IR and the
// extensible problem model in the scheduling infrastructure.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_SSP_SSPUTILITIES_H
#define CIRCT_DIALECT_SSP_SSPUTILITIES_H

#include "circt/Dialect/SSP/SSPAttributes.h"
#include "circt/Dialect/SSP/SSPOps.h"
#include "circt/Scheduling/Problems.h"
#include "circt/Support/ValueMapper.h"

#include "mlir/IR/ImplicitLocOpBuilder.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/TypeSwitch.h"

#include <functional>

namespace circt {
namespace ssp {

using OperatorType = scheduling::Problem::OperatorType;
using Dependence = scheduling::Problem::Dependence;

template <typename ProblemT>
void loadOperationProperties(ProblemT &, Operation *, ArrayAttr) {}
template <typename ProblemT, typename OperationPropertyT,
          typename... OperationPropertyTs>
void loadOperationProperties(ProblemT &prob, Operation *op, ArrayAttr props) {
  if (!props)
    return;
  for (auto prop : props) {
    TypeSwitch<Attribute>(prop)
        .Case<OperationPropertyT, OperationPropertyTs...>(
            [&](auto p) { p.setInProblem(prob, op); });
  }
}

template <typename ProblemT>
void loadOperatorTypeProperties(ProblemT &, OperatorType, ArrayAttr) {}
template <typename ProblemT, typename OperatorTypePropertyT,
          typename... OperatorTypePropertyTs>
void loadOperatorTypeProperties(ProblemT &prob, OperatorType opr,
                                ArrayAttr props) {
  if (!props)
    return;
  for (auto prop : props) {
    TypeSwitch<Attribute>(prop)
        .Case<OperatorTypePropertyT, OperatorTypePropertyTs...>(
            [&](auto p) { p.setInProblem(prob, opr); });
  }
}

template <typename ProblemT>
void loadDependenceProperties(ProblemT &, Dependence, ArrayAttr) {}
template <typename ProblemT, typename DependencePropertyT,
          typename... DependencePropertyTs>
void loadDependenceProperties(ProblemT &prob, Dependence dep, ArrayAttr props) {
  if (!props)
    return;
  for (auto prop : props) {
    TypeSwitch<Attribute>(prop)
        .Case<DependencePropertyT, DependencePropertyTs...>(
            [&](auto p) { p.setInProblem(prob, dep); });
  }
}

template <typename ProblemT>
void loadInstanceProperties(ProblemT &, ArrayAttr) {}
template <typename ProblemT, typename InstancePropertyT,
          typename... InstancePropertyTs>
void loadInstanceProperties(ProblemT &prob, ArrayAttr props) {
  if (!props)
    return;
  for (auto prop : props) {
    TypeSwitch<Attribute>(prop).Case<InstancePropertyT, InstancePropertyTs...>(
        [&](auto p) { p.setInProblem(prob); });
  }
}

template <typename ProblemT, typename... OperationPropertyTs,
          typename... OperatorTypePropertyTs, typename... DependencePropertyTs,
          typename... InstancePropertyTs>
ProblemT loadProblem(InstanceOp instOp,
                     std::tuple<OperationPropertyTs...> opProps,
                     std::tuple<OperatorTypePropertyTs...> oprProps,
                     std::tuple<DependencePropertyTs...> depProps,
                     std::tuple<InstancePropertyTs...> instProps) {
  auto prob = ProblemT::get(instOp);

  loadInstanceProperties<ProblemT, InstancePropertyTs...>(
      prob, instOp.getPropertiesAttr());

  instOp.walk([&](OperatorTypeOp oprOp) {
    OperatorType opr = oprOp.getNameAttr();
    prob.insertOperatorType(opr);
    loadOperatorTypeProperties<ProblemT, OperatorTypePropertyTs...>(
        prob, opr, oprOp.getPropertiesAttr());
  });

  // Register all operations first, in order to retain their original order.
  instOp.walk([&](OperationOp opOp) {
    prob.insertOperation(opOp);
    loadOperationProperties<ProblemT, OperationPropertyTs...>(
        prob, opOp, opOp.getPropertiesAttr());
  });

  // Then walk them again, and load auxiliary dependences as well as any
  // dependence properties.
  instOp.walk([&](OperationOp opOp) {
    ArrayAttr depsAttr = opOp.getDependencesAttr();
    if (!depsAttr)
      return;

    for (auto depAttr : depsAttr.getAsRange<DependenceAttr>()) {
      Dependence dep;
      if (FlatSymbolRefAttr sourceRef = depAttr.getSourceRef()) {
        Operation *sourceOp = SymbolTable::lookupSymbolIn(instOp, sourceRef);
        assert(sourceOp);
        dep = Dependence(sourceOp, opOp);
        LogicalResult res = prob.insertDependence(dep);
        assert(succeeded(res));
        (void)res;
      } else
        dep = Dependence(&opOp->getOpOperand(depAttr.getOperandIdx()));

      loadDependenceProperties<ProblemT, DependencePropertyTs...>(
          prob, dep, depAttr.getProperties());
    }
  });

  return prob;
}

template <typename ProblemT, typename... OperationPropertyTs>
ArrayAttr saveOperationProperties(ProblemT &prob, Operation *op,
                                  ImplicitLocOpBuilder &b) {
  SmallVector<Attribute> props;
  Attribute prop;
  ((prop = OperationPropertyTs::getFromProblem(prob, op, b.getContext()),
    prop ? props.push_back(prop) : (void)prop),
   ...);
  return props.empty() ? ArrayAttr() : b.getArrayAttr(props);
}

template <typename ProblemT, typename... OperatorTypePropertyTs>
ArrayAttr saveOperatorTypeProperties(ProblemT &prob, OperatorType opr,
                                     ImplicitLocOpBuilder &b) {
  SmallVector<Attribute> props;
  Attribute prop;
  ((prop = OperatorTypePropertyTs::getFromProblem(prob, opr, b.getContext()),
    prop ? props.push_back(prop) : (void)prop),
   ...);
  return props.empty() ? ArrayAttr() : b.getArrayAttr(props);
}

template <typename ProblemT, typename... DependencePropertyTs>
ArrayAttr saveDependenceProperties(ProblemT &prob, Dependence dep,
                                   ImplicitLocOpBuilder &b) {
  SmallVector<Attribute> props;
  Attribute prop;
  ((prop = DependencePropertyTs::getFromProblem(prob, dep, b.getContext()),
    prop ? props.push_back(prop) : (void)prop),
   ...);
  return props.empty() ? ArrayAttr() : b.getArrayAttr(props);
}

template <typename ProblemT, typename... InstancePropertyTs>
ArrayAttr saveInstanceProperties(ProblemT &prob, ImplicitLocOpBuilder &b) {
  SmallVector<Attribute> props;
  Attribute prop;
  ((prop = InstancePropertyTs::getFromProblem(prob, b.getContext()),
    prop ? props.push_back(prop) : (void)prop),
   ...);
  return props.empty() ? ArrayAttr() : b.getArrayAttr(props);
}

template <typename ProblemT, typename... OperationPropertyTs,
          typename... OperatorTypePropertyTs, typename... DependencePropertyTs,
          typename... InstancePropertyTs>
InstanceOp
saveProblem(ProblemT &prob, StringAttr instanceName, StringAttr problemName,
            std::function<StringAttr(Operation *)> operationNameFn,
            std::tuple<OperationPropertyTs...> opProps,
            std::tuple<OperatorTypePropertyTs...> oprProps,
            std::tuple<DependencePropertyTs...> depProps,
            std::tuple<InstancePropertyTs...> instProps, OpBuilder &builder) {
  ImplicitLocOpBuilder b(builder.getUnknownLoc(), builder);

  // Set up instance.
  auto instOp = b.create<ssp::InstanceOp>(
      instanceName, problemName,
      saveInstanceProperties<ProblemT, InstancePropertyTs...>(prob, b));

  b.setInsertionPointToStart(&instOp.getBody().getBlocks().front());

  // Emit operator types.
  for (auto opr : prob.getOperatorTypes())
    b.create<OperatorTypeOp>(
        opr, saveOperatorTypeProperties<ProblemT, OperatorTypePropertyTs...>(
                 prob, opr, b));

  // Determine which operations act as source ops for auxiliary dependences, and
  // therefore need a name. Also, honor names provided by the client.
  DenseMap<Operation *, StringAttr> opNames;
  for (auto *op : prob.getOperations()) {
    if (StringAttr providedName = operationNameFn(op))
      opNames[op] = providedName;

    for (auto &dep : prob.getDependences(op)) {
      Operation *src = dep.getSource();
      if (!dep.isAuxiliary() || opNames.count(src))
        continue;
      if (StringAttr providedName = operationNameFn(src)) {
        opNames[src] = providedName;
        continue;
      }
      opNames[src] = b.getStringAttr(Twine("Op") + Twine(opNames.size()));
    }
  }

  // Construct operations and model their dependences.
  BackedgeBuilder backedgeBuilder(b, b.getLoc());
  ValueMapper v(&backedgeBuilder);
  for (auto *op : prob.getOperations()) {
    // Construct the `dependences attribute`. It contains `DependenceAttr` for
    // def-use deps _with_ properties, and all aux deps.
    ArrayAttr dependences;
    SmallVector<Attribute> depAttrs;
    unsigned auxOperandIdx = op->getNumOperands();
    for (auto &dep : prob.getDependences(op)) {
      ArrayAttr depProps =
          saveDependenceProperties<ProblemT, DependencePropertyTs...>(prob, dep,
                                                                      b);
      if (dep.isDefUse() && depProps) {
        auto depAttr = b.getAttr<DependenceAttr>(*dep.getDestinationIndex(),
                                                 FlatSymbolRefAttr(), depProps);
        depAttrs.push_back(depAttr);
        continue;
      }

      if (!dep.isAuxiliary())
        continue;

      auto sourceOpName = opNames.lookup(dep.getSource());
      assert(sourceOpName);
      auto sourceRef = b.getAttr<FlatSymbolRefAttr>(sourceOpName);
      auto depAttr =
          b.getAttr<DependenceAttr>(auxOperandIdx, sourceRef, depProps);
      depAttrs.push_back(depAttr);
      ++auxOperandIdx;
    }
    if (!depAttrs.empty())
      dependences = b.getArrayAttr(depAttrs);

    // Delegate to helper to construct the `properties` attribute.
    ArrayAttr properties =
        saveOperationProperties<ProblemT, OperationPropertyTs...>(prob, op, b);

    // Finally, create the `OperationOp` and inform the value mapper.
    // NB: sym_name, dependences and properties are optional attributes, so
    // passing potentially unitialized String/ArrayAttrs is intentional here.
    auto opOp =
        b.create<OperationOp>(op->getNumResults(), v.get(op->getOperands()),
                              opNames.lookup(op), dependences, properties);
    v.set(op->getResults(), opOp->getResults());
  }

  return instOp;
}

} // namespace ssp
} // namespace circt

#endif // CIRCT_DIALECT_SSP_SSPUTILITIES_H