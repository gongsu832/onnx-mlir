/*
 * SPDX-License-Identifier: Apache-2.0
 */

//===--------------- LSTM.cpp - Lowering LSTM Op --------------------------===//
//
// Copyright 2019 The IBM Research Authors.
//
// =============================================================================
//
// This file lowers the ONNX LSTM Operators to Krnl dialect.
//
//===----------------------------------------------------------------------===//

#include "src/Conversion/ONNXToKrnl/RNN/RNNBase.hpp"
#include "src/Dialect/ONNX/MLIRDialectBuilder.hpp"

#define TEST_FUSED_MATMUL false

using namespace mlir;

struct LstmState {
  // returned states.
  Value allH;
  Value ht;
  Value ct;
  // intermediate states.
  Value forwardHt;
  Value reverseHt;
  Value forwardCt;
  Value reverseCt;
};

struct LstmActivationPack {
  RNNActivation f;
  RNNActivation g;
  RNNActivation h;
};

struct LstmWeightPack {
  Value W;
  Value R;
  Value Wi;
  Value Wo;
  Value Wf;
  Value Wc;
  Value Ri;
  Value Ro;
  Value Rf;
  Value Rc;
};

struct LstmBiasPack {
  bool hasBias = false;
  Value Wbi;
  Value Wbo;
  Value Wbf;
  Value Wbc;
  Value Rbi;
  Value Rbo;
  Value Rbf;
  Value Rbc;
  // Put peephole here.
  bool hasPeephole = false;
  Value Pi;
  Value Po;
  Value Pf;
};

template <>
bool hasAllNoneOutput<ONNXLSTMOp>(ONNXLSTMOp *op) {
  return (
      isNoneType(op->Y()) && isNoneType(op->Y_h()) && isNoneType(op->Y_c()));
}

template <>
std::tuple<LstmActivationPack, LstmActivationPack>
getActivationPack<ONNXLSTMOp, LstmActivationPack>(ONNXLSTMOp *op) {
  auto direction = op->direction();
  auto activations = op->activations();
  auto activationAlpha = op->activation_alpha();
  auto activationBeta = op->activation_beta();

  LstmActivationPack activationForward, activationReverse;

  // Get activation function name.
  // Default forward functions
  activationForward.f.name = "sigmoid";
  activationForward.g.name = "tanh";
  activationForward.h.name = "tanh";
  // Default backward functions
  activationReverse.f.name = "sigmoid";
  activationReverse.g.name = "tanh";
  activationReverse.h.name = "tanh";
  if (activations) {
    ArrayAttr activationArrAttr = activations.getValue();
    if (direction == FORWARD || direction == BIDIRECTIONAL) {
      // Forward activations.
      if (activationArrAttr.size() > 0) {
        activationForward.f.name =
            activationArrAttr[0].cast<StringAttr>().getValue();
      }
      if (activationArrAttr.size() > 1) {
        activationForward.g.name =
            activationArrAttr[1].cast<StringAttr>().getValue();
      }
      if (activationArrAttr.size() > 2) {
        activationForward.h.name =
            activationArrAttr[2].cast<StringAttr>().getValue();
      }
    }

    // Reverse activations.
    if (direction == REVERSE || direction == BIDIRECTIONAL) {
      unsigned int startIndex = (direction == REVERSE) ? 0 : 3;
      if (activationArrAttr.size() > startIndex) {
        activationReverse.f.name =
            activationArrAttr[startIndex].cast<StringAttr>().getValue();
      }
      if (activationArrAttr.size() > startIndex + 1) {
        activationReverse.g.name =
            activationArrAttr[startIndex + 1].cast<StringAttr>().getValue();
      }
      if (activationArrAttr.size() > startIndex + 2) {
        activationReverse.h.name =
            activationArrAttr[startIndex + 2].cast<StringAttr>().getValue();
      }
    }
  }

  // Get alpha attributes.
  if (activationAlpha) {
    ArrayAttr activationArrAttr = activationAlpha.getValue();
    if (direction == FORWARD || direction == BIDIRECTIONAL) {
      // Forward activations.
      if (activationArrAttr.size() > 0) {
        activationForward.f.alpha = activationArrAttr[0].cast<FloatAttr>();
      }
      if (activationArrAttr.size() > 1) {
        activationForward.g.alpha = activationArrAttr[1].cast<FloatAttr>();
      }
      if (activationArrAttr.size() > 2) {
        activationForward.h.alpha = activationArrAttr[2].cast<FloatAttr>();
      }
    }

    // Reverse activations.
    if (direction == REVERSE || direction == BIDIRECTIONAL) {
      unsigned int startIndex = (direction == REVERSE) ? 0 : 3;
      if (activationArrAttr.size() > startIndex) {
        activationReverse.f.alpha =
            activationArrAttr[startIndex].cast<FloatAttr>();
      }
      if (activationArrAttr.size() > startIndex + 1) {
        activationReverse.g.alpha =
            activationArrAttr[startIndex + 1].cast<FloatAttr>();
      }
      if (activationArrAttr.size() > startIndex + 2) {
        activationReverse.h.alpha =
            activationArrAttr[startIndex + 2].cast<FloatAttr>();
      }
    }
  }

  // Get beta attributes.
  if (activationBeta) {
    ArrayAttr activationArrAttr = activationBeta.getValue();
    if (direction == FORWARD || direction == BIDIRECTIONAL) {
      // Forward activations.
      if (activationArrAttr.size() > 0) {
        activationForward.f.beta = activationArrAttr[0].cast<FloatAttr>();
      }
      if (activationArrAttr.size() > 1) {
        activationForward.g.beta = activationArrAttr[1].cast<FloatAttr>();
      }
      if (activationArrAttr.size() > 2) {
        activationForward.h.beta = activationArrAttr[2].cast<FloatAttr>();
      }
    }

    // Reverse activations.
    if (direction == REVERSE || direction == BIDIRECTIONAL) {
      unsigned int startIndex = (direction == REVERSE) ? 0 : 3;
      if (activationArrAttr.size() > startIndex) {
        activationReverse.f.beta =
            activationArrAttr[startIndex].cast<FloatAttr>();
      }
      if (activationArrAttr.size() > startIndex + 1) {
        activationReverse.g.beta =
            activationArrAttr[startIndex + 1].cast<FloatAttr>();
      }
      if (activationArrAttr.size() > startIndex + 2) {
        activationReverse.h.beta =
            activationArrAttr[startIndex + 2].cast<FloatAttr>();
      }
    }
  }

  return std::make_tuple(activationForward, activationReverse);
}

template <>
std::tuple<LstmWeightPack, LstmWeightPack>
getWeightPack<ONNXLSTMOp, LstmWeightPack>(
    ConversionPatternRewriter &rewriter, Location loc, ONNXLSTMOp *op) {
  // Return values.
  LstmWeightPack weightForward, weightReverse;

  // parameter weight: [direction, 4*hiddenSize, inputSize]
  Value W = op->W();
  // recurrence weight: [direction, 4*hiddenSize, hiddenSize]
  Value R = op->R();
  // direction
  StringRef direction = op->direction();

  ArrayRef<int64_t> wShape = W.getType().cast<ShapedType>().getShape();
  Type elementType = W.getType().cast<ShapedType>().getElementType();
  int64_t hiddenSize = wShape[1] / 4;
  int64_t inputSize = wShape[2];

  // MemRef types for parameter weights.
  auto w3DTy = MemRefType::get({1, 4 * hiddenSize, inputSize}, elementType);
  auto w2DTy = MemRefType::get({4 * hiddenSize, inputSize}, elementType);
  SmallVector<Type, 4> w3D2Ty(2, w3DTy);

  // MemRef types for recurrence weights.
  auto r3DTy = MemRefType::get({1, 4 * hiddenSize, hiddenSize}, elementType);
  auto r2DTy = MemRefType::get({4 * hiddenSize, hiddenSize}, elementType);
  SmallVector<Type, 4> r3D2Ty(2, r3DTy);

  // Squeeze the direction axis from W and R.
  Value fW, bW, fR, bR;
  if (direction == FORWARD) {
    fW = foldOrEmitONNXSqueezeOp(rewriter, loc, w2DTy, W, /*axis=*/0);
    fR = foldOrEmitONNXSqueezeOp(rewriter, loc, r2DTy, R, /*axis=*/0);
  } else if (direction == REVERSE) {
    bW = foldOrEmitONNXSqueezeOp(rewriter, loc, w2DTy, W, /*axis=*/0);
    bR = foldOrEmitONNXSqueezeOp(rewriter, loc, r2DTy, R, /*axis=*/0);
  } else { // BIDIRECTIONAL
    // W
    std::vector<Value> vals =
        foldOrEmitONNXSplitOp(rewriter, loc, w3D2Ty, W, 0);
    fW = foldOrEmitONNXSqueezeOp(rewriter, loc, w2DTy, vals[0], /*axis=*/0);
    bW = foldOrEmitONNXSqueezeOp(rewriter, loc, w2DTy, vals[1], /*axis=*/0);
    // R
    vals.clear();
    vals = foldOrEmitONNXSplitOp(rewriter, loc, r3D2Ty, R, 0);
    fR = foldOrEmitONNXSqueezeOp(rewriter, loc, r2DTy, vals[0], /*axis=*/0);
    bR = foldOrEmitONNXSqueezeOp(rewriter, loc, r2DTy, vals[1], /*axis=*/0);
  }

  // Split W and R into individual weight tensors, and transpose them.
  if (direction == FORWARD || direction == BIDIRECTIONAL) {
    // W
    weightForward.W = fW;
    // R
    weightForward.R = fR;
  }
  if (direction == REVERSE || direction == BIDIRECTIONAL) {
    // W
    weightReverse.W = bW;
    // R
    weightReverse.R = bR;
  }
  return std::make_tuple(weightForward, weightReverse);
}

template <>
std::tuple<LstmBiasPack, LstmBiasPack> getBiasPack<ONNXLSTMOp, LstmBiasPack>(
    ConversionPatternRewriter &rewriter, Location loc, ONNXLSTMOp *op) {
  // Return values.
  LstmBiasPack biasForward, biasReverse;

  // bias: [direction, 8*hiddenSize] for both parameter and recurrence weights.
  Value B = op->B();
  // peephold: [direction, 3*hiddenSize] for input, output and forget gates.
  Value P = op->P();

  // direction
  StringRef direction = op->direction();

  // Split B.
  if (!isNoneType(B)) {
    ArrayRef<int64_t> bShape = B.getType().cast<ShapedType>().getShape();
    Type elementType = B.getType().cast<ShapedType>().getElementType();
    int64_t hiddenSize = bShape[1] / 8;

    // MemRef types.
    auto bType2D = MemRefType::get({1, 8 * hiddenSize}, elementType);
    auto bType1D = MemRefType::get({8 * hiddenSize}, elementType);
    auto bSplitType1D = MemRefType::get({hiddenSize}, elementType);
    SmallVector<Type, 4> split1D8Ty(8, bSplitType1D);
    SmallVector<Type, 4> split2D2Ty(2, bType2D);

    // Squeeze the direction axis from B.
    Value fB, bB;
    if (direction == FORWARD) {
      fB = foldOrEmitONNXSqueezeOp(rewriter, loc, bType1D, B, /*axis=*/0);
    } else if (direction == REVERSE) {
      bB = foldOrEmitONNXSqueezeOp(rewriter, loc, bType1D, B, /*axis=*/0);
    } else { // BIDIRECTIONAL
      std::vector<Value> vals;
      vals = foldOrEmitONNXSplitOp(rewriter, loc, split2D2Ty, B, 0);
      fB = foldOrEmitONNXSqueezeOp(rewriter, loc, bType1D, vals[0], /*axis=*/0);
      bB = foldOrEmitONNXSqueezeOp(rewriter, loc, bType1D, vals[1], /*axis=*/0);
    }

    // Split B into individual bias tensors.
    if (direction == FORWARD || direction == BIDIRECTIONAL) {
      std::vector<Value> vals =
          foldOrEmitONNXSplitOp(rewriter, loc, split1D8Ty, fB, 0);
      biasForward.Wbi = vals[0];
      biasForward.Wbo = vals[1];
      biasForward.Wbf = vals[2];
      biasForward.Wbc = vals[3];
      biasForward.Rbi = vals[4];
      biasForward.Rbo = vals[5];
      biasForward.Rbf = vals[6];
      biasForward.Rbc = vals[7];
      biasForward.hasBias = true;
    }
    if (direction == REVERSE || direction == BIDIRECTIONAL) {
      std::vector<Value> vals =
          foldOrEmitONNXSplitOp(rewriter, loc, split1D8Ty, bB, 0);
      biasReverse.Wbi = vals[0];
      biasReverse.Wbo = vals[1];
      biasReverse.Wbf = vals[2];
      biasReverse.Wbc = vals[3];
      biasReverse.Rbi = vals[4];
      biasReverse.Rbo = vals[5];
      biasReverse.Rbf = vals[6];
      biasReverse.Rbc = vals[7];
      biasReverse.hasBias = true;
    }
  }

  // Split P.
  if (!isNoneType(P)) {
    ArrayRef<int64_t> pShape = P.getType().cast<ShapedType>().getShape();
    Type elementType = P.getType().cast<ShapedType>().getElementType();
    int64_t hiddenSize = pShape[1] / 3;

    // MemRef types.
    auto pType2D = MemRefType::get({1, 3 * hiddenSize}, elementType);
    auto pType1D = MemRefType::get({3 * hiddenSize}, elementType);
    auto pSplitType1D = MemRefType::get({hiddenSize}, elementType);
    SmallVector<Type, 4> split1D3Ty(3, pSplitType1D);
    SmallVector<Type, 4> split2D2Ty(2, pType2D);

    // Squeeze the direction axis from P.
    Value fP, bP;
    if (direction == FORWARD) {
      fP = foldOrEmitONNXSqueezeOp(rewriter, loc, pType1D, P, /*axis=*/0);
    } else if (direction == REVERSE) {
      bP = foldOrEmitONNXSqueezeOp(rewriter, loc, pType1D, P, /*axis=*/0);
    } else { // BIDIRECTIONAL
      std::vector<Value> vals =
          foldOrEmitONNXSplitOp(rewriter, loc, split2D2Ty, P, 0);
      fP = foldOrEmitONNXSqueezeOp(rewriter, loc, pType1D, vals[0], /*axis=*/0);
      bP = foldOrEmitONNXSqueezeOp(rewriter, loc, pType1D, vals[1], /*axis=*/0);
    }

    // Split P into individual tensors.
    if (direction == FORWARD || direction == BIDIRECTIONAL) {
      std::vector<Value> vals =
          foldOrEmitONNXSplitOp(rewriter, loc, split1D3Ty, fP, 0);
      biasForward.Pi = vals[0];
      biasForward.Po = vals[1];
      biasForward.Pf = vals[2];
      biasForward.hasPeephole = true;
    }
    if (direction == REVERSE || direction == BIDIRECTIONAL) {
      std::vector<Value> vals =
          foldOrEmitONNXSplitOp(rewriter, loc, split1D3Ty, bP, 0);
      biasReverse.Pi = vals[0];
      biasReverse.Po = vals[1];
      biasReverse.Pf = vals[2];
      biasReverse.hasPeephole = true;
    }
  }

  return std::make_tuple(biasForward, biasReverse);
}

template <>
LstmState allocAndInitializeStates<ONNXLSTMOp, LstmState>(
    ConversionPatternRewriter &rewriter, Location loc, ONNXLSTMOp *op,
    typename ONNXLSTMOp::Adaptor operandAdaptor) {
  LstmState state;

  // direction
  StringRef direction = op->direction();

  // Insert allocation and deallocation for the results of this operation.
  // If the result is not returned, then no allocation happens.
  // Y :: [seq_length, num_directions, batch_size, hidden_size]
  state.allH = allocAllHidden(rewriter, loc, operandAdaptor.X(),
      operandAdaptor.W(), operandAdaptor.R(), op->Y(),
      checkInsertDealloc(op->getOperation(), 0));
  // Y_h :: [num_directions, batch_size, hidden_size]
  state.ht = allocHiddenOrCell(rewriter, loc, operandAdaptor.X(),
      operandAdaptor.W(), operandAdaptor.R(), op->Y_h(),
      checkInsertDealloc(op->getOperation(), 1));
  // Y_c :: [num_directions, batch_size, hidden_size]
  state.ct = allocHiddenOrCell(rewriter, loc, operandAdaptor.X(),
      operandAdaptor.W(), operandAdaptor.R(), op->Y_c(),
      checkInsertDealloc(op->getOperation(), 2));

  // Insert allocation and deallocation the intermediate Ht and Ct for the
  // forward and reverse directions.
  // Ht :: [batch_size, hidden_size]
  // Ct :: [batch_size, hidden_size]
  if (direction == FORWARD || direction == BIDIRECTIONAL) {
    state.forwardHt = allocIntermediateState(
        rewriter, loc, operandAdaptor.X(), operandAdaptor.R());
    state.forwardCt = allocIntermediateState(
        rewriter, loc, operandAdaptor.X(), operandAdaptor.R());
  }
  if (direction == REVERSE || direction == BIDIRECTIONAL) {
    state.reverseHt = allocIntermediateState(
        rewriter, loc, operandAdaptor.X(), operandAdaptor.R());
    state.reverseCt = allocIntermediateState(
        rewriter, loc, operandAdaptor.X(), operandAdaptor.R());
  }

  // Initialize Ht and Ct.
  initializeIntermediateStates(rewriter, loc, state.forwardHt, state.reverseHt,
      state.forwardCt, state.reverseCt, operandAdaptor.initial_h(),
      operandAdaptor.initial_c(),
      operandAdaptor.X().getType().cast<MemRefType>().getElementType(),
      direction, /*onlyHidden=*/false);
  return state;
}

template <>
void calculateState<LstmState, LstmActivationPack, LstmWeightPack,
    LstmBiasPack>(ConversionPatternRewriter &rewriter, Location loc, Value Xt,
    LstmState state, LstmActivationPack activationPack,
    LstmWeightPack weightPack, LstmBiasPack biasPack, Value sequenceIV,
    Value directionIV, bool isForward) {
  // Equations for LSTM.
  // it = f(Xt*(Wi^T) + Ht-1*(Ri^T) + Pi (.) Ct-1 + Wbi + Rbi)
  // ft = f(Xt*(Wf^T) + Ht-1*(Rf^T) + Pf (.) Ct-1 + Wbf + Rbf)
  // ct = g(Xt*(Wc^T) + Ht-1*(Rc^T) + Wbc + Rbc)
  // Ct = ft (.) Ct-1 + it (.) ct
  // ot = f(Xt*(Wo^T) + Ht-1*(Ro^T) + Po (.) Ct + Wbo + Rbo)
  // Ht = ot (.) h(Ct)

  // TODO remove scope
  ImplicitLocOpBuilder lb(loc, rewriter);
  KrnlBuilder createKrnl(lb);

  ArrayRef<int64_t> xtShape = Xt.getType().cast<ShapedType>().getShape();
  int64_t batchSize = xtShape[0];
  int64_t inputSize = xtShape[1];

  // Get Ht, Ct.
  Value Ht = (isForward) ? state.forwardHt : state.reverseHt;
  Value Ct = (isForward) ? state.forwardCt : state.reverseCt;

  ArrayRef<int64_t> htShape = Ht.getType().cast<ShapedType>().getShape();
  int64_t hiddenSize = htShape[1];

  // Frequently used types.
  MemRefType matrixType = Ht.getType().cast<MemRefType>();

  // Do matrix multiplications.
  Value XtWi, XtWf, XtWc, XtWo, HtRi, HtRf, HtRc, HtRo;
  Value XtW, HtR;
  if (TEST_FUSED_MATMUL) {
    // For testing purpose, support only static dimensions.
    Type elementType = matrixType.getElementType();
    Value zero = lb.create<ConstantIndexOp>(0);
    Value zeroVal = emitConstantOp(rewriter, loc, elementType, 0);
    XtWi = lb.create<memref::AllocOp>(matrixType);
    XtWf = lb.create<memref::AllocOp>(matrixType);
    XtWc = lb.create<memref::AllocOp>(matrixType);
    XtWo = lb.create<memref::AllocOp>(matrixType);
    emitFusedMatMul(rewriter, loc, matrixType, Xt,
        {weightPack.Wi, weightPack.Wf, weightPack.Wc, weightPack.Wo}, zero,
        zeroVal, {XtWi, XtWf, XtWc, XtWo});
    HtRi = lb.create<memref::AllocOp>(matrixType);
    HtRf = lb.create<memref::AllocOp>(matrixType);
    HtRc = lb.create<memref::AllocOp>(matrixType);
    HtRo = lb.create<memref::AllocOp>(matrixType);
    emitFusedMatMul(rewriter, loc, matrixType, Ht,
        {weightPack.Ri, weightPack.Rf, weightPack.Rc, weightPack.Ro}, zero,
        zeroVal, {HtRi, HtRf, HtRc, HtRo});
  } else {
    Type elementType = matrixType.getElementType();
    OnnxBuilder createONNX(createKrnl);

    ArrayAttr permAttr = rewriter.getI64ArrayAttr({1, 0});

    auto xtTranspose2DTy = MemRefType::get({inputSize, batchSize}, elementType);
    auto xtTranspose =
        foldOrEmitONNXTransposeOp(rewriter, loc, xtTranspose2DTy, Xt, permAttr);
    XtW = createONNX.matmul(
        MemRefType::get({4 * hiddenSize, batchSize}, elementType), weightPack.W,
        xtTranspose);

    auto htTranspose2DTy =
        MemRefType::get({hiddenSize, batchSize}, elementType);
    auto htTranspose =
        foldOrEmitONNXTransposeOp(rewriter, loc, htTranspose2DTy, Ht, permAttr);

    HtR = createONNX.matmul(
        MemRefType::get({4 * hiddenSize, batchSize}, elementType), weightPack.R,
        htTranspose);
  }

  // Do element-wise computations. Fuse them into a single nested loop.
  // Lower and upper bounds derived from Ht tensor.
  unsigned HtRank = matrixType.getRank();
  Value iZero = lb.create<ConstantIndexOp>(0);
  SmallVector<Value, 4> HtLbs(HtRank, iZero);
  SmallVector<Value, 4> HtUbs;
  for (unsigned r = 0; r < HtRank; ++r) {
    Value idx = lb.create<ConstantIndexOp>(r);
    HtUbs.emplace_back(lb.createOrFold<memref::DimOp>(Ht, idx));
  }

  ValueRange loops = createKrnl.defineLoops(HtRank);
  createKrnl.iterate(loops, loops, HtLbs, HtUbs, {},
      [&](KrnlBuilder &createKrnl, ValueRange args) {
        MathBuilder createMath(createKrnl);
        IndexExprScope ieScope(createKrnl);
        ValueRange indices = createKrnl.getInductionVarValue(loops);
        Value bs(indices[0]), hs(indices[1]);
        SymbolIndexExpr bsie(bs), hsie(hs);

        Value CtVal = createKrnl.load(Ct, indices);
        // it = f(Xt*(Wi^T) + Ht-1*(Ri^T) + Pi (.) Ct-1 + Wbi + Rbi)
        Value XtWiVal = createKrnl.loadIE(XtW, {hsie, bsie});
        Value HtRiVal = createKrnl.loadIE(HtR, {hsie, bsie});
        Value it = createMath.add(XtWiVal, HtRiVal);
        if (biasPack.hasBias) {
          Value WbiVal = createKrnl.load(biasPack.Wbi, {hs});
          Value RbiVal = createKrnl.load(biasPack.Rbi, {hs});
          it = createMath.add(it, WbiVal);
          it = createMath.add(it, RbiVal);
        }
        if (biasPack.hasPeephole) {
          Value PiVal = createKrnl.load(biasPack.Pi, {hs});
          Value PiCt = createMath.mul(PiVal, CtVal);
          it = createMath.add(it, PiCt);
        }
        it =
            applyActivation(createKrnl.getBuilder(), loc, activationPack.f, it);

        // ft = f(Xt*(Wf^T) + Ht-1*(Rf^T) + Pf (.) Ct-1 + Wbf + Rbf)
        Value XtWfVal = createKrnl.loadIE(XtW, {hsie + 2 * hiddenSize, bsie});
        Value HtRfVal = createKrnl.loadIE(HtR, {hsie + 2 * hiddenSize, bsie});
        Value ft = createMath.add(XtWfVal, HtRfVal);
        if (biasPack.hasBias) {
          Value WbfVal = createKrnl.load(biasPack.Wbf, {hs});
          Value RbfVal = createKrnl.load(biasPack.Rbf, {hs});
          ft = createMath.add(ft, WbfVal);
          ft = createMath.add(ft, RbfVal);
        }
        if (biasPack.hasPeephole) {
          Value PfVal = createKrnl.load(biasPack.Pf, {hs});
          Value PfCt = createMath.mul(PfVal, CtVal);
          ft = createMath.add(ft, PfCt);
        }
        ft =
            applyActivation(createKrnl.getBuilder(), loc, activationPack.f, ft);

        // ct = g(Xt*(Wc^T) + Ht-1*(Rc^T) + Wbc + Rbc)
        Value XtWcVal = createKrnl.loadIE(XtW, {hsie + 3 * hiddenSize, bsie});
        Value HtRcVal = createKrnl.loadIE(HtR, {hsie + 3 * hiddenSize, bsie});
        Value ct = createMath.add(XtWcVal, HtRcVal);
        if (biasPack.hasBias) {
          Value WbcVal = createKrnl.load(biasPack.Wbc, {hs});
          Value RbcVal = createKrnl.load(biasPack.Rbc, {hs});
          ct = createMath.add(ct, WbcVal);
          ct = createMath.add(ct, RbcVal);
        }
        ct =
            applyActivation(createKrnl.getBuilder(), loc, activationPack.g, ct);

        // Ct = ft (.) Ct-1 + it (.) ct
        Value ftCt = createMath.mul(ft, CtVal);
        Value itct = createMath.mul(it, ct);
        Value nextCt = createMath.add(ftCt, itct);

        // ot = f(Xt*(Wo^T) + Ht-1*(Ro^T) + Po (.) Ct + Wbo + Rbo)
        Value XtWoVal = createKrnl.loadIE(XtW, {hsie + hiddenSize, bsie});
        Value HtRoVal = createKrnl.loadIE(HtR, {hsie + hiddenSize, bsie});
        Value ot = createMath.add(XtWoVal, HtRoVal);
        if (biasPack.hasBias) {
          Value WboVal = createKrnl.load(biasPack.Wbo, {hs});
          Value RboVal = createKrnl.load(biasPack.Rbo, {hs});
          ot = createMath.add(ot, WboVal);
          ot = createMath.add(ot, RboVal);
        }
        if (biasPack.hasPeephole) {
          Value PoVal = createKrnl.load(biasPack.Po, {hs});
          Value PoCt = createMath.mul(PoVal, nextCt);
          ot = createMath.add(ot, PoCt);
        }
        ot =
            applyActivation(createKrnl.getBuilder(), loc, activationPack.f, ot);

        // Ht = ot (.) h(Ct)
        Value nextHt = applyActivation(
            createKrnl.getBuilder(), loc, activationPack.h, nextCt);
        nextHt = createMath.mul(ot, nextHt);

        // Store the intermediate Ht, Ct.
        createKrnl.store(nextCt, Ct, indices);
        createKrnl.store(nextHt, Ht, indices);
        if (!isNoneType(state.allH))
          createKrnl.store(
              nextHt, state.allH, {sequenceIV, directionIV, bs, hs});
      });

  if (TEST_FUSED_MATMUL) {
    rewriter.create<memref::DeallocOp>(loc, XtWi);
    rewriter.create<memref::DeallocOp>(loc, XtWf);
    rewriter.create<memref::DeallocOp>(loc, XtWc);
    rewriter.create<memref::DeallocOp>(loc, XtWo);
    rewriter.create<memref::DeallocOp>(loc, HtRi);
    rewriter.create<memref::DeallocOp>(loc, HtRf);
    rewriter.create<memref::DeallocOp>(loc, HtRc);
    rewriter.create<memref::DeallocOp>(loc, HtRo);
  }
}

template <>
void stateToOutput<ONNXLSTMOp, LstmState>(ConversionPatternRewriter &rewriter,
    Location loc, ONNXLSTMOp *op, LstmState state,
    std::vector<Value> &outputs) {
  Value noneValue;
  auto direction = op->direction();

  // First output: all sequences.
  outputs.emplace_back((isNoneType(op->Y()) ? noneValue : state.allH));
  // Second output: hidden.
  if (isNoneType(op->Y_h()))
    outputs.emplace_back(noneValue);
  else {
    stateToOutputForHiddenOrCell(
        rewriter, loc, state.forwardHt, state.reverseHt, direction, state.ht);
    outputs.emplace_back(state.ht);
  }
  // Third output: cell.
  if (isNoneType(op->Y_c()))
    outputs.emplace_back(noneValue);
  else {
    stateToOutputForHiddenOrCell(
        rewriter, loc, state.forwardCt, state.reverseCt, direction, state.ct);
    outputs.emplace_back(state.ct);
  }
}

void populateLoweringONNXLSTMOpPattern(
    OwningRewritePatternList &patterns, MLIRContext *ctx) {
  patterns.insert<ONNXRNNOpLowering<ONNXLSTMOp, LstmState, LstmActivationPack,
      LstmWeightPack, LstmBiasPack>>(ctx);
}
