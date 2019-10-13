/* This file is part of:
 * Operon - Large Scale Genetic Programming Framework
 *
 * Copyright (C) 2019 Bogdan Burlacu 
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 * SOFTWARE.
 */

#ifndef OS_RECOMBINATOR_HPP
#define OS_RECOMBINATOR_HPP

#include "core/operator.hpp"

namespace Operon {
template <typename TEvaluator, typename TSelector, typename TCrossover, typename TMutator>
class OffspringSelectionRecombinator : public RecombinatorBase<TEvaluator, TSelector, TCrossover, TMutator> {
public:
    explicit OffspringSelectionRecombinator(TEvaluator& eval, TSelector& sel, TCrossover& cx, TMutator& mut)
        : RecombinatorBase<TEvaluator, TSelector, TCrossover, TMutator>(eval, sel, cx, mut)
    {
    }

    using T = typename TSelector::SelectableType;
    std::optional<T> operator()(operon::rand_t& random, double pCrossover, double pMutation) const override
    {
        std::uniform_real_distribution<double> uniformReal;
        bool doCrossover = uniformReal(random) < pCrossover;
        bool doMutation = uniformReal(random) < pMutation;

        if (!(doCrossover || doMutation))
            return std::nullopt;

        constexpr bool Max = TSelector::Maximization;
        constexpr gsl::index Idx = TSelector::SelectableIndex;
        auto population = this->Selector().Population();

        auto first = this->selector(random);
        auto fit = population[first][Idx];

        typename TSelector::SelectableType child;

        if (doCrossover) {
            auto second = this->selector(random);
            child.Genotype = this->crossover(random, population[first].Genotype, population[second].Genotype);

            if constexpr (TSelector::Maximization) {
                fit = std::max(fit, population[second][Idx]);
            } else {
                fit = std::min(fit, population[second][Idx]);
            }
        }

        if (doMutation) {
            child.Genotype = doCrossover
                ? this->mutator(random, std::move(child.Genotype))
                : this->mutator(random, population[first].Genotype);
        }

        std::conditional_t<Max, std::less<>, std::greater<>> comp;

        child[Idx] = this->evaluator(random, child);

        if (std::isfinite(child[Idx]) && comp(fit, child[Idx])) {
            return std::make_optional(child);
        }
        return std::nullopt;
    }

    void MaxSelectionPressure(size_t value) { maxSelectionPressure = value; }
    size_t MaxSelectionPressure() const { return maxSelectionPressure; }

    void Prepare(const gsl::span<const T> pop) const override
    {
        this->Selector().Prepare(pop);
        lastEvaluations = this->evaluator.get().FitnessEvaluations();
    }

    double SelectionPressure() const
    {
        if (this->Selector().Population().empty()) {
            return 0;
        }
        return (this->evaluator.get().FitnessEvaluations() - lastEvaluations) / static_cast<double>(this->Selector().Population().size());
    }

    bool Terminate() const override
    {
        return RecombinatorBase<TEvaluator, TSelector, TCrossover, TMutator>::Terminate() || SelectionPressure() > maxSelectionPressure;
    };

private:
    mutable size_t lastEvaluations;
    size_t maxSelectionPressure;
};
} // namespace Operon

#endif
