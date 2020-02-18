#include "Dialect/Stencil/Passes.h"
#include "Dialect/Stencil/StencilDialect.h"
#include "Dialect/Stencil/StencilOps.h"
#include "mlir/Dialect/Linalg/IR/LinalgTraits.h"
#include "mlir/Dialect/StandardOps/Ops.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Function.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/Region.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/IR/UseDefLists.h"
#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/Passes.h"
#include "mlir/Transforms/Utils.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/raw_ostream.h"
#include <bits/stdint-intn.h>
#include <cstddef>
#include <iterator>
#include <tuple>
#include <utility>

using namespace mlir;

namespace {

// // Helper method that computes an early insertion point
// Operation *getInsertionPoint(Operation *op) {
//   // Insert after the operand computed last
//   Operation *ip = nullptr;
//   for (auto operand : op->getOperands()) {
//     if (auto definingOp = operand.getDefiningOp()) {
//       if (!ip || ip->isBeforeInBlock(definingOp))
//         ip = definingOp;
//     }
//   }
//   // Return the insertion point
//   return ip;
// }

// // Helper method that returns true if op can be moved
// bool canMoveArithmeticOp(Operation *op) {
//   auto ip = getInsertionPoint(op);
//   return ip && ip->getNextNode() != op;
// }

// Helper method skipping unary operations
// Operation *skipUnaryOperations(Operation *op) {
//   while (auto negOp = dyn_cast_or_null<NegFOp>(op)) {
//     op = op->getOperand(0).getDefiningOp();
//   }
//   return op;
// }

// Helper method that returns true if first argument is produced before
bool isProducedBefore(Value before, Value after) {
  auto beforeOp = before.getDefiningOp();
  auto afterOp = after.getDefiningOp();
  if (beforeOp && afterOp) {
    return beforeOp->isBeforeInBlock(afterOp);
  }
  return before == nullptr;
}

// bool isProducedLast(Value val1, Value val2, Value val3) {
//   llvm::outs() << "isProducedLast " << (isProducedBefore(val2, val1) && isProducedBefore(val3, val1)) << "\n";
//   return isProducedBefore(val2, val1) && isProducedBefore(val3, val1);
// }

// bool isArithmeticBinaryOp(Operation *op) {
//   return isa<AddFOp>(op);
// }

// Helper method to check if a value was produced by a specific operation type
template<typename TOp>
bool isProducedBy(Value val) {
  if(auto definingOp = val.getDefiningOp()) 
    return isa<TOp>(definingOp);
  return false;
}
// template<typename TOp1, typename TOp2>
// bool isProducedBy(Value val) {
//   if(auto definingOp = val.getDefiningOp()) 
//     return isa<TOp1>(definingOp) || isa<TOp2>(definingOp);
//   return false;
// }


// // Helper method to move arithmetic op
// template <typename TOp>
// SmallVector<Value, 4> moveArithmeticOp(PatternRewriter &rewriter, TOp op) {
//   rewriter.setInsertionPointAfter(getInsertionPoint(op.getOperation()));
//   auto clonedOp = rewriter.clone(*op.getOperation());
//   clonedOp->getBlock()->recomputeOpOrder();
//   return clonedOp->getResults();
// }

#include "Dialect/Stencil/StencilShufflePatterns.cpp.inc"

struct StencilShufflePass
    : public OperationPass<StencilShufflePass, stencil::ApplyOp> {
  void runOnOperation() override;
};

void StencilShufflePass::runOnOperation() {
  auto applyOp = getOperation();
  
  OwningRewritePatternList patterns;
  populateWithGenerated(&getContext(), &patterns);
  applyPatternsGreedily(applyOp, patterns);
}

} // namespace

std::unique_ptr<OpPassBase<stencil::ApplyOp>>
stencil::createStencilShufflePass() {
  return std::make_unique<StencilShufflePass>();
}
