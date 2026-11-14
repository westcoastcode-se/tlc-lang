// The set of locals that are not fully initialized, as a persistent map.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace tlc {

// Maps an initialization id to one bit per leaf, 1 where that leaf is set. A fully set local is absent.
//
// The checker copies this state for each expression, for the true and the false side of a condition.
// A plain map made that copy linear in the open locals, and a function with many open locals quadratic.
// This is a radix trie whose nodes are shared and never changed: a copy shares the root, a change copies
// one path, and a merge keeps a subtree that both sides share without walking it.
class OpenMap {
  public:
    // The bits of `id`, or null when `id` is fully initialized or not tracked.
    const std::vector<uint8_t> *get(int id) const {
        const Node *node = root_.get();
        for (int level = 0; level < kLevels && node != nullptr; ++level) {
            node = node->kids[digit(id, level)].get();
        }
        return node == nullptr ? nullptr : node->value.get();
    }

    bool contains(int id) const { return get(id) != nullptr; }

    // `bits` has at least one 0. A fully set local is erased instead, so a subtree both sides of a join
    // share joins to itself.
    void set(int id, std::vector<uint8_t> bits) {
        root_ = put(root_, static_cast<uint32_t>(id), 0, std::make_shared<const std::vector<uint8_t>>(std::move(bits)));
    }

    void erase(int id) {
        if (contains(id)) {
            root_ = put(root_, static_cast<uint32_t>(id), 0, nullptr);
        }
    }

    // Where control flow joins. A leaf is set only where it is set on both sides. A local that is open
    // on one side is open after the join, and one that both sides set fully is absent.
    static OpenMap merge(const OpenMap &lhs, const OpenMap &rhs) {
        OpenMap out;
        out.root_ = join(lhs.root_, rhs.root_, 0);
        return out;
    }

  private:
    struct Node {
        std::array<std::shared_ptr<const Node>, 16> kids{};
        // Set on a node at the last level.
        std::shared_ptr<const std::vector<uint8_t>> value;
    };
    using Ref = std::shared_ptr<const Node>;

    // Eight levels of four bits cover every non-negative `int` id.
    static constexpr int kLevels = 8;

    static int digit(int id, int level) {
        return static_cast<int>((static_cast<uint32_t>(id) >> (4 * (kLevels - 1 - level))) & 15u);
    }

    static bool empty_node(const Node &node) {
        if (node.value != nullptr) {
            return false;
        }
        for (const Ref &kid : node.kids) {
            if (kid != nullptr) {
                return false;
            }
        }
        return true;
    }

    static Ref put(const Ref &node, uint32_t id, int level, std::shared_ptr<const std::vector<uint8_t>> value) {
        if (level == kLevels) {
            if (value == nullptr) {
                return nullptr;
            }
            auto leaf = std::make_shared<Node>();
            leaf->value = std::move(value);
            return leaf;
        }
        if (node == nullptr && value == nullptr) {
            return nullptr;
        }
        auto copy = node != nullptr ? std::make_shared<Node>(*node) : std::make_shared<Node>();
        const int at = digit(static_cast<int>(id), level);
        copy->kids[at] = put(node != nullptr ? node->kids[at] : nullptr, id, level + 1, std::move(value));
        if (empty_node(*copy)) {
            return nullptr;
        }
        return copy;
    }

    static Ref join(const Ref &lhs, const Ref &rhs, int level) {
        // A shared subtree joins to itself. One side alone keeps its open locals.
        if (lhs == rhs) {
            return lhs;
        }
        if (lhs == nullptr) {
            return rhs;
        }
        if (rhs == nullptr) {
            return lhs;
        }
        if (level == kLevels) {
            const std::vector<uint8_t> &left = *lhs->value;
            const std::vector<uint8_t> &right = *rhs->value;
            const std::size_t width = left.size() > right.size() ? left.size() : right.size();
            std::vector<uint8_t> bits(width, 0);
            bool all = width > 0;
            for (std::size_t i = 0; i < width; ++i) {
                const uint8_t bit = (i < left.size() ? left[i] : 0) & (i < right.size() ? right[i] : 0);
                bits[i] = bit;
                if (bit == 0) {
                    all = false;
                }
            }
            if (all) {
                return nullptr;
            }
            auto leaf = std::make_shared<Node>();
            leaf->value = std::make_shared<const std::vector<uint8_t>>(std::move(bits));
            return leaf;
        }
        auto out = std::make_shared<Node>();
        for (std::size_t i = 0; i < out->kids.size(); ++i) {
            out->kids[i] = join(lhs->kids[i], rhs->kids[i], level + 1);
        }
        if (empty_node(*out)) {
            return nullptr;
        }
        return out;
    }

    Ref root_;
};

} // namespace tlc
