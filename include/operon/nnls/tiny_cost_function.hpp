// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2019-2022 Heal Research

#ifndef OPERON_NNLS_TINY_OPTIMIZER
#define OPERON_NNLS_TINY_OPTIMIZER

#include <Eigen/Core>
#include "operon/interpreter/interpreter.hpp"

namespace Operon {

// this cost function is adapted to work with both solvers from Ceres: the normal one and the tiny solver
// for this, a number of template parameters are necessary:
// - the CostFunctor is the actual functor for computing the residuals
// - the JetT type represents a dual number, the user can specify the type for the Scalar part (float, double) and the Stride (Ceres-specific)
// - the StorageOrder specifies the format of the jacobian (row-major for the big Ceres solver, column-major for the tiny solver)

template <typename CostFunctor, typename JetT, int StorageOrder = Eigen::RowMajor>
struct TinyCostFunction {
    using Scalar = typename JetT::Scalar;
    static constexpr int Stride = JetT::DIMENSION;
    static constexpr int Storage = StorageOrder;

    enum {
        NUM_RESIDUALS = Eigen::Dynamic,  // NOLINT
        NUM_PARAMETERS = Eigen::Dynamic, // NOLINT
    };

    explicit TinyCostFunction(CostFunctor const& functor)
        : functor_(functor)
    {
    }

    auto Evaluate(Scalar const* parameters, Scalar* residuals, Scalar* jacobian) const -> bool
    {
        if (jacobian == nullptr) {
            return functor_(&parameters, residuals);
        }

        auto numParameters = NumParameters();
        auto numResiduals = NumResiduals();

        // Allocate scratch space for the strided evaluation.
        Operon::Vector<JetT> inputJets(numParameters);
        Operon::Vector<JetT> outputJets(numResiduals);

        auto ptr = &inputJets[0];

        for (int j = 0; j < numParameters; ++j) {
            inputJets[j].a = parameters[j];
        }

        int currentDerivativeSection = 0;
        int currentDerivativeSectionCursor = 0;

        Eigen::Map<Eigen::Matrix<Scalar, -1, -1, StorageOrder>> jMap(jacobian, numResiduals, numParameters);

        // Evaluate all of the strides. Each stride is a chunk of the derivative to
        // evaluate, typically some size proportional to the size of the SIMD
        // registers of the CPU.
        int numStrides = static_cast<int>(
            std::ceil(static_cast<float>(numParameters) / static_cast<float>(Stride)));

        for (int pass = 0; pass < numStrides; ++pass) {
            // Set most of the jet components to zero, except for
            // non-constant #Stride parameters.
            const int initialDerivativeSection = currentDerivativeSection;
            const int initialDerivativeSectionCursor = currentDerivativeSectionCursor;

            int activeParameterCount = 0;
            for (int j = 0; j < numParameters; ++j) {
                inputJets[j].v.setZero();
                if (activeParameterCount < Stride && j >= currentDerivativeSectionCursor) {
                    inputJets[j].v[activeParameterCount] = 1.0;
                    ++activeParameterCount;
                }
            }

            if (!functor_(&ptr, &outputJets[0])) {
                return false;
            }

            activeParameterCount = 0;
            currentDerivativeSection = initialDerivativeSection;
            currentDerivativeSectionCursor = initialDerivativeSectionCursor;

            // Copy the pieces of the jacobians into their final place.
            for (int j = currentDerivativeSectionCursor; j < numParameters; ++j) {
                if (activeParameterCount < Stride) {
                    for (int k = 0; k < numResiduals; ++k) {
                        jMap(k, j) = outputJets[k].v[activeParameterCount];
                    }
                    ++activeParameterCount;
                    ++currentDerivativeSectionCursor;
                }
            }

            // Only copy the residuals over once (even though we compute them on every loop).
            if (pass == numStrides - 1) {
                std::transform(outputJets.begin(), outputJets.end(), residuals, [](auto const& jet) { return jet.a; });
            }
        }
        return true;
    }

    // required by tiny solver
    auto operator()(Scalar const* parameters, Scalar* residuals, Scalar* jacobian) const -> bool
    {
        return Evaluate(parameters, residuals, jacobian);
    }

    [[nodiscard]] auto NumResiduals() const -> int { return functor_.NumResiduals(); }
    [[nodiscard]] auto NumParameters() const -> int { return functor_.NumParameters(); }

private:
    CostFunctor functor_;
};
} // namespace Operon

#endif
