// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2019-2022 Heal Research

#ifndef TREE_HPP
#define TREE_HPP

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "operon/core/node.hpp"
#include "operon/operon_export.hpp"
#include "operon/hash/hash.hpp"
#include "contracts.hpp"

namespace Operon {

template<typename T>
class SubtreeIterator {
public:
    // iterator traits
    using value_type = std::conditional_t<std::is_const_v<T>, Node const, Node>;// NOLINT
    using difference_type = std::ptrdiff_t;// NOLINT
    using pointer = value_type*;// NOLINT
    using reference = value_type&;// NOLINT
    using iterator_category = std::forward_iterator_tag;// NOLINT

    explicit SubtreeIterator(T& tree, size_t i)
        : nodes_(tree.Nodes())
        , parentIndex_(i)
        , index_(i - 1)
    {
        EXPECT(i > 0);
    }

    inline auto operator*() -> value_type& { return nodes_[index_]; }
    inline auto operator*() const -> value_type const& { return nodes_[index_]; }
    inline auto operator->() -> value_type* { return &**this; }
    inline auto operator->() const -> value_type const* { return &**this; }

    auto operator++() -> SubtreeIterator& // pre-increment
    {
        index_ -= nodes_[index_].Length + 1UL;
        return *this;
    }

    auto operator++(int) -> SubtreeIterator // post-increment
    {
        auto t = *this;
        ++t;
        return t;
    }

    auto operator==(SubtreeIterator const& rhs) -> bool
    {
        return std::tie(index_, parentIndex_, nodes_.data()) == std::tie(rhs.index_, rhs.parentIndex_, rhs.nodes_.data());
    }

    auto operator!=(SubtreeIterator const& rhs) -> bool
    {
        return !(*this == rhs);
    }

    auto operator<(SubtreeIterator const& rhs) -> bool
    {
        // this looks a little strange, but correct: we use a postfix representation and trees are iterated from right to left
        // (thus the lower index will be the more advanced iterator)
        return std::tie(parentIndex_, nodes_.data()) == std::tie(rhs.parentIndex_, rhs.nodes_.data()) && index_ > rhs.index_;
    }

    inline auto HasNext() -> bool { return index_ < parentIndex_ && index_ >= (parentIndex_ - nodes_[parentIndex_].Length); }
    [[nodiscard]] inline auto Index() const -> size_t { return index_; } // index of current child

private:
    Operon::Span<value_type> nodes_;
    size_t parentIndex_; // index of parent node
    size_t index_;       // index of current child node
};

class OPERON_EXPORT Tree { // NOLINT
public:
    Tree() = default;
    Tree(std::initializer_list<Node> list)
        : nodes_(list)
    {
    }
    explicit Tree(Operon::Vector<Node> vec)
        : nodes_(std::move(vec))
    {
    }
    Tree(Tree const& rhs) // NOLINT
        : nodes_(rhs.nodes_)
    {
    }
    Tree(Tree&& rhs) noexcept
        : nodes_(std::move(rhs.nodes_))
    {
    }

    ~Tree() = default;

    auto operator=(Tree rhs) -> Tree&
    {
        Swap(*this, rhs);
        return *this;
    }
    // no need for move assignment operator because we use the copy-swap idiom

    friend void Swap(Tree& lhs, Tree& rhs) noexcept
    {
        std::swap(lhs.nodes_, rhs.nodes_);
    }

    auto UpdateNodes() -> Tree&;
    auto Sort() -> Tree&;
    auto Reduce() -> Tree&;
    auto Simplify() -> Tree&;

    // convenience method to make it easier to call from the Python side
    auto Hash(Operon::HashFunction f, Operon::HashMode m) -> Tree&;

    // performs hashing in a manner similar to Merkle trees
    // aggregating hash values from the leafs towards the root node
    template <Operon::HashFunction H>
    auto Hash(Operon::HashMode mode) noexcept -> Tree&
    {
        std::vector<size_t> childIndices;
        childIndices.reserve(nodes_.size());

        std::vector<Operon::Hash> hashes;
        hashes.reserve(nodes_.size());

        Operon::Hasher<H> hasher;

        for (size_t i = 0; i < nodes_.size(); ++i) {
            auto& n = nodes_[i];

            if (n.IsLeaf()) {
                n.CalculatedHashValue = n.HashValue;
                if (mode == Operon::HashMode::Strict) {
                    const size_t s1 = sizeof(Operon::Hash);
                    const size_t s2 = sizeof(Operon::Scalar);
                    std::array<uint8_t, s1 + s2> key{};
                    auto* ptr = key.data();
                    std::memcpy(ptr, &n.HashValue, s1);
                    std::memcpy(ptr + s1, &n.Value, s2);
                    n.CalculatedHashValue = hasher(key.data(), key.size());
                } else {
                    n.CalculatedHashValue = n.HashValue;
                }
                continue;
            }

            for (auto it = Children(i); it.HasNext(); ++it) {
                childIndices.push_back(it.Index());
            }

            auto begin = childIndices.begin();
            auto end = begin + n.Arity;

            if (n.IsCommutative()) {
                std::stable_sort(begin, end, [&](auto a, auto b) { return nodes_[a] < nodes_[b]; });
            }
            std::transform(begin, end, std::back_inserter(hashes), [&](auto j) { return nodes_[j].CalculatedHashValue; });
            hashes.push_back(n.HashValue);

            n.CalculatedHashValue = hasher(reinterpret_cast<uint8_t*>(hashes.data()), sizeof(Operon::Hash) * hashes.size()); // NOLINT
            childIndices.clear();
            hashes.clear();
        }

        return *this;
    }

    auto Subtree(size_t i) -> Tree {
        auto const& n = nodes_[i];
        Operon::Vector<Node> subtree;
        subtree.reserve(n.Length);
        std::copy_n(nodes_.begin() + std::make_signed_t<size_t>(i) - n.Length, n.Length, std::back_inserter(subtree));
        return Tree(subtree).UpdateNodes();
    }

    [[nodiscard]] auto ChildIndices(size_t i) const -> std::vector<size_t>;
    inline void SetEnabled(size_t i, bool enabled)
    {
        for (auto j = i - nodes_[i].Length; j <= i; ++j) {
            nodes_[j].IsEnabled = enabled;
        }
    }

    auto Nodes() & -> Operon::Vector<Node>& { return nodes_; }
    auto Nodes() && -> Operon::Vector<Node>&& { return std::move(nodes_); }
    [[nodiscard]] auto Nodes() const& -> Operon::Vector<Node> const& { return nodes_; }

    [[nodiscard]] inline auto CoefficientsCount() const
    {
        return std::count_if(nodes_.cbegin(), nodes_.cend(), [](auto const& s) { return s.IsLeaf(); });
    }

    void SetCoefficients(Operon::Span<Operon::Scalar const> coefficients);
    [[nodiscard]] auto GetCoefficients() const -> std::vector<Operon::Scalar>;

    inline auto operator[](size_t i) noexcept -> Node& { return nodes_[i]; }
    inline auto operator[](size_t i) const noexcept -> Node const& { return nodes_[i]; }

    [[nodiscard]] auto Length() const noexcept -> size_t { return nodes_.size(); }
    [[nodiscard]] auto VisitationLength() const noexcept -> size_t;
    [[nodiscard]] auto Depth() const noexcept -> size_t;
    [[nodiscard]] auto Empty() const noexcept -> bool { return nodes_.empty(); }

    [[nodiscard]] auto HashValue() const -> Operon::Hash { return nodes_.empty() ? 0 : nodes_.back().CalculatedHashValue; }

    auto Children(size_t i) -> SubtreeIterator<Tree> { return SubtreeIterator(*this, i); }
    [[nodiscard]] auto Children(size_t i) const -> SubtreeIterator<Tree const> { return SubtreeIterator(*this, i); }

private:
    Operon::Vector<Node> nodes_;
};
} // namespace Operon
#endif // TREE_H

