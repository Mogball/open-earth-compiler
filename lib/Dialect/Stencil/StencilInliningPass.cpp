#include "Dialect/Stencil/Passes.h"
#include "Dialect/Stencil/StencilDialect.h"
#include "Dialect/Stencil/StencilOps.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Function.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/Region.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/IR/UseDefLists.h"
#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/Passes.h"
#include "mlir/Transforms/Utils.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <bits/stdint-intn.h>
#include <cstddef>
#include <iterator>

using namespace mlir;

namespace {

// Pattern introducing a root stencil if two stencils share an input
struct RootRewrite : public OpRewritePattern<stencil::LoadOp> {
  RootRewrite(MLIRContext *context)
      : OpRewritePattern<stencil::LoadOp>(context, /*benefit=*/1) {}

  // Helper method introducing common root
  LogicalResult introduceRoot(stencil::LoadOp loadOp,
                              ArrayRef<Operation*> users,
                              PatternRewriter &rewriter) const {
    // Introduce the root op right after the load op
    auto loc = loadOp.getLoc();
    // Find the earliest user in block
    Operation *insertionPoint = users.front();
    for(auto user: users) {
      if (user->isBeforeInBlock(insertionPoint))
        insertionPoint = user;
    }
    // Clone the the load op and reroute output through root op
    auto clonedOp = rewriter.clone(*loadOp.getOperation());
    rewriter.setInsertionPoint(insertionPoint);
    auto rootOp = rewriter.create<stencil::ApplyOp>(loc, clonedOp->getResults(),
                                                    clonedOp->getResults());
    rootOp.region().push_back(new Block());
    rootOp.region().front().addArgument(clonedOp->getResult(0).getType());
    rewriter.setInsertionPointToEnd(rootOp.getBody());
    // Create the body of the root op
    SmallVector<int64_t, 3> zeroOffset = {0, 0, 0};
    auto accessOp = rewriter.create<stencil::AccessOp>(
        loc, rootOp.getBody()->getArgument(0), zeroOffset);
    rewriter.create<stencil::ReturnOp>(
        loc, accessOp.getOperation()->getResults(), nullptr);
    // Replace all uses of the load Op results
    // (except for the use by rootOp)
    rewriter.replaceOp(loadOp, rootOp.getResults());
    return success();
  }

  LogicalResult matchAndRewrite(stencil::LoadOp loadOp,
                                PatternRewriter &rewriter) const override {
    // Count users of the load op
    SmallVector<Operation*,10> users;
    for (auto user : loadOp.getResult().getUsers()) {
      if (isa_and_nonnull<stencil::ApplyOp>(user))
        users.push_back(user);
    }

    // Introduce a common stencil root for all stencils loading the input
    if (users.size() > 1) {
      return introduceRoot(loadOp, users, rewriter);
    }
    return failure();
  }
};

// Pattern rerouting output edge via consumer
struct RerouteRewrite : public OpRewritePattern<stencil::ApplyOp> {
  RerouteRewrite(MLIRContext *context)
      : OpRewritePattern<stencil::ApplyOp>(context, /*benefit=*/2) {}

  // Helper method inlining the consumer in the producer
  LogicalResult redirectStore(stencil::ApplyOp producerOp,
                              stencil::ApplyOp consumerOp,
                              PatternRewriter &rewriter) const {
    // Clone the producer op
    rewriter.setInsertionPointAfter(producerOp);
    auto clonedOp = rewriter.clone(*producerOp.getOperation());

    // Compute operand and result lists
    SmallVector<Value, 10> newOperands = consumerOp.getOperands();
    SmallVector<Value, 10> newResults = consumerOp.getResults();
    for (unsigned i = 0, e = producerOp.getNumResults(); i != e; ++i) {
      // Count the result uses
      auto uses = producerOp.getResult(i).getUses();
      size_t count = std::distance(uses.begin(), uses.end());
      // Result of cloned operation
      auto result = clonedOp->getResult(i);
      // Replace the producer of result in the operands
      auto it = llvm::find(newOperands, producerOp.getResult(i));
      // Add the result if multiple uses and replace by cloned result
      if (it != std::end(newOperands) && count > 1) {
        *it = result;
        newResults.push_back(result);
      }
      // Add parameter and result if not consumed but has uses
      if (it == std::end(newOperands) && count > 0) {
        newOperands.push_back(result);
        newResults.push_back(result);
      }
    }

    // Create new consumer op right after the producer op
    auto loc = consumerOp.getLoc();
    auto newOp =
        rewriter.create<stencil::ApplyOp>(loc, newOperands, newResults);
    newOp.region().push_back(new Block());
    // Clone the body of the consumer op
    BlockAndValueMapping mapper;
    for (size_t i = 0, e = newOperands.size(); i != e; ++i) {
      newOp.getBody()->addArgument(newOperands[i].getType());
      if (i < consumerOp.getNumOperands())
        mapper.map(consumerOp.getBody()->getArgument(i),
                   newOp.getBody()->getArgument(i));
    }
    rewriter.setInsertionPointToStart(newOp.getBody());
    for (auto &op : consumerOp.getBody()->getOperations()) {
      rewriter.clone(op, mapper);
    }

    // Get the terminator of the cloned consumer op
    auto returnOp =
        dyn_cast<stencil::ReturnOp>(newOp.getBody()->getTerminator());
    rewriter.setInsertionPoint(returnOp);

    // Add access to load the rerouted parameters
    SmallVector<Value, 10> returnOperands = returnOp.getOperands();
    for (auto result : clonedOp->getResults()) {
      if (llvm::is_contained(newResults, result)) {
        // Compute the argument index and add an access op
        auto it = llvm::find(newOperands, result);
        size_t index =
            std::distance(newOperands.begin(), llvm::find(newOperands, result));
        SmallVector<int64_t, 3> zeroOffset = {0, 0, 0};
        auto accessOp = rewriter.create<stencil::AccessOp>(
            loc, newOp.getBody()->getArgument(index), zeroOffset);
        returnOperands.push_back(accessOp.getResult());
      }
    }

    // Replace the return op
    rewriter.create<stencil::ReturnOp>(loc, returnOperands, nullptr);
    rewriter.eraseOp(returnOp);

    // Replace the producer and consumer ops
    SmallVector<Value, 10> newConsumerRes;
    SmallVector<Value, 10> newProducerRes;
    for (unsigned i = 0, e = consumerOp.getNumResults(); i != e; ++i) {
      newConsumerRes.push_back(newOp.getResult(i));
    }
    for (unsigned i = 0, e = producerOp.getNumResults(); i != e; ++i) {
      auto it = llvm::find(newResults, clonedOp->getResult(i));
      if (it != newResults.end()) {
        newProducerRes.push_back(
            newOp.getResult(std::distance(newResults.begin(), it)));
      } else {
        newProducerRes.push_back(clonedOp->getResult(i));
      }
    }
    rewriter.replaceOp(producerOp, newProducerRes);
    rewriter.replaceOp(consumerOp, newConsumerRes);
    return success();
  }

  LogicalResult matchAndRewrite(stencil::ApplyOp applyOp,
                                PatternRewriter &rewriter) const override {
    // Search consumer connected to a single producer
    SmallVector<Operation *, 10> producerOps;
    for (auto operand : applyOp.operands()) {
      if (isa_and_nonnull<stencil::ApplyOp>(operand.getDefiningOp())) {
        if (!llvm::is_contained(producerOps, operand.getDefiningOp()))
          producerOps.push_back(operand.getDefiningOp());
      }
    }

    // Redirect outputs of the producer
    if (producerOps.size() == 1) {
      // Collect all consumers
      SmallVector<Operation *, 10> consumerOps;
      for (auto result : producerOps[0]->getOpResults()) {
        consumerOps.append(result.getUsers().begin(), result.getUsers().end());
      }

      // Verify the other operands are before the producer
      for (auto operand : applyOp.getOperands()) {
        if (operand.getDefiningOp() &&
            producerOps[0]->isBeforeInBlock(operand.getDefiningOp()))
          return failure();
      }

      // Verify the producer has multiple consumers
      if (llvm::all_of(consumerOps, [&](Operation *op) {
            return op == applyOp.getOperation();
          }))
        return failure();

      // Redirect the producer outputs via the consumer
      return redirectStore(cast<stencil::ApplyOp>(producerOps.front()), applyOp,
                           rewriter);
    }
    return failure();
  }
};

// Pattern inlining producer into consumer
// (assuming the producer has only a single consumer)
struct InliningRewrite : public OpRewritePattern<stencil::ApplyOp> {
  InliningRewrite(MLIRContext *context)
      : OpRewritePattern<stencil::ApplyOp>(context, /*benefit=*/3) {}

  // Helper method replacing all uses of temporary by inline computation
  void replaceAccess(stencil::ApplyOp consumerOp, stencil::AccessOp accessOp,
                     ValueRange producerResults, stencil::ReturnOp returnOp,
                     PatternRewriter &rewriter) const {
    for (unsigned i = 0, e = consumerOp.getNumOperands(); i != e; ++i) {
      if (consumerOp.getBody()->getArgument(i) == accessOp.view()) {
        size_t index = std::distance(
            producerResults.begin(),
            llvm::find(producerResults, consumerOp.getOperand(i)));
        assert(index < returnOp.getNumOperands() &&
               "failed to find inlined computation");
        rewriter.replaceOp(accessOp, returnOp.getOperand(index));
        break;
      }
    }
  }

  // Helper method inlining the producer computation
  LogicalResult inlineProducer(stencil::ApplyOp producerOp,
                               stencil::ApplyOp consumerOp,
                               ValueRange producerResults,
                               PatternRewriter &rewriter) const {
    // Compute the operand list and an argument mapper befor cloning
    BlockAndValueMapping mapper;
    SmallVector<Value, 10> newOperands;
    for (unsigned i = 0, e = consumerOp.getNumOperands(); i != e; ++i) {
      if (llvm::is_contained(producerResults, consumerOp.getOperand(i)))
        mapper.map(consumerOp.getBody()->getArgument(i),
                   consumerOp.getBody()->getArgument(i));
      else
        newOperands.push_back(consumerOp.getOperand(i));
    }
    for (auto operand : producerOp.getOperands()) {
      if (!llvm::is_contained(newOperands, operand)) {
        newOperands.push_back(operand);
        consumerOp.getBody()->addArgument(operand.getType());
      }
    }

    // Clone the consumer op
    auto loc = consumerOp.getLoc();
    auto newOp = rewriter.create<stencil::ApplyOp>(loc, newOperands,
                                                   consumerOp.getResults());
    rewriter.cloneRegionBefore(consumerOp.region(), newOp.region(),
                               newOp.region().begin(), mapper);

    // Add mappings for the producer operands
    for (unsigned i = 0, e = producerOp.getNumOperands(); i != e; ++i) {
      auto it = llvm::find(newOperands, producerOp.getOperand(i));
      assert(it != newOperands.end() && "expected to find producer operand");
      mapper.map(
          producerOp.getBody()->getArgument(i),
          newOp.getBody()->getArgument(std::distance(newOperands.begin(), it)));
    }

    // Walk accesses of producer results and replace them by computation
    newOp.walk([&](stencil::AccessOp accessOp) {
      if (llvm::count(newOp.getBody()->getArguments(), accessOp.view()) == 0) {
        SmallVector<int64_t, 3> offset = accessOp.getOffset();
        // Copy the operations in after the access op
        rewriter.setInsertionPoint(accessOp);
        for (auto &op : producerOp.getBody()->getOperations()) {
          auto clonedOp = rewriter.clone(op, mapper);
          clonedOp->walk([&](stencil::AccessOp accessOp) {
            SmallVector<int64_t, 3> sum(offset.size());
            llvm::transform(llvm::zip(offset, accessOp.getOffset()),
                            sum.begin(), [](std::tuple<int64_t, int64_t> x) {
                              return std::get<0>(x) + std::get<1>(x);
                            });
            accessOp.setOffset(sum);
          });
        }

        // Replace all uses of the accesOp
        stencil::ReturnOp returnOp =
            cast<stencil::ReturnOp>(*std::prev(Block::iterator(accessOp)));
        replaceAccess(consumerOp, accessOp, producerResults, returnOp,
                      rewriter);
        rewriter.eraseOp(returnOp);
      }
    });

    // Update the all uses and copy the loop bounds
    rewriter.replaceOp(consumerOp, newOp.getResults());
    return success();
  }

  LogicalResult matchAndRewrite(stencil::ApplyOp applyOp,
                                PatternRewriter &rewriter) const override {
    // Search producer apply op
    for (auto operand : applyOp.operands()) {
      if (isa_and_nonnull<stencil::ApplyOp>(operand.getDefiningOp())) {
        // Collect consumer ops of the producer
        SmallVector<Operation *, 10> consumerOps;
        auto producerResults = operand.getDefiningOp()->getResults();
        for (auto result : producerResults) {
          consumerOps.append(result.getUsers().begin(),
                             result.getUsers().end());
        }

        // Try the next producer if current has multiple consumers
        if (llvm::any_of(consumerOps, [&](Operation *op) {
              return op != applyOp.getOperation();
            }))
          continue;

        // If there is only a single consumer perform the inlining
        return inlineProducer(cast<stencil::ApplyOp>(operand.getDefiningOp()),
                              applyOp, producerResults, rewriter);
      }
    }
    return failure();
  }
};

struct StencilInliningPass : public FunctionPass<StencilInliningPass> {
  void runOnFunction() override;
};

void StencilInliningPass::runOnFunction() {
  FuncOp funcOp = getFunction();
  OwningRewritePatternList patterns;
  patterns.insert<InliningRewrite, RerouteRewrite, RootRewrite>(&getContext());
  applyPatternsGreedily(funcOp, patterns);
}

} // namespace

std::unique_ptr<OpPassBase<FuncOp>> mlir::stencil::createStencilInliningPass() {
  return std::make_unique<StencilInliningPass>();
}

static PassRegistration<StencilInliningPass> pass("stencil-inlining",
                                                  "Inline stencil apply ops");
