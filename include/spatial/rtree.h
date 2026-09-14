#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tick_db {

struct GenericMBR {
    std::vector<int64_t> min_bounds;
    std::vector<int64_t> max_bounds;

    GenericMBR() = default;
    explicit GenericMBR(size_t dims)
        : min_bounds(dims, std::numeric_limits<int64_t>::max()),
          max_bounds(dims, std::numeric_limits<int64_t>::min()) {}

    bool is_empty() const { return min_bounds.empty(); }
    size_t dimensions() const { return min_bounds.size(); }

    bool intersects(const GenericMBR& other) const {
        if (is_empty() || other.is_empty()) return false;
        size_t dims = std::min(min_bounds.size(), other.min_bounds.size());
        for (size_t i = 0; i < dims; ++i) {
            if (max_bounds[i] < other.min_bounds[i] || min_bounds[i] > other.max_bounds[i]) {
                return false;  // Subtree pruned on dimension i!
            }
        }
        return true;
    }

    void expand(const GenericMBR& other) {
        if (other.is_empty()) return;
        if (is_empty()) {
            min_bounds = other.min_bounds;
            max_bounds = other.max_bounds;
            return;
        }
        size_t dims = std::min(min_bounds.size(), other.min_bounds.size());
        for (size_t i = 0; i < dims; ++i) {
            min_bounds[i] = std::min(min_bounds[i], other.min_bounds[i]);
            max_bounds[i] = std::max(max_bounds[i], other.max_bounds[i]);
        }
    }
};

struct PageMBR {
    uint32_t page_id{0};
    GenericMBR mbr;
    uint32_t record_count{0};
};

class HierarchicalRTree;

struct RTreeEntry {
    GenericMBR mbr;
    uint32_t row_group_id{0};
    std::string filepath;
    std::vector<PageMBR> page_mbrs;
    std::shared_ptr<HierarchicalRTree> page_rtree;
};

struct RTreeNode {
    bool is_leaf{true};
    GenericMBR mbr;
    std::vector<RTreeEntry> entries;                   // Populated if is_leaf == true
    std::vector<std::shared_ptr<RTreeNode>> children;  // Populated if is_leaf == false
};

class HierarchicalRTree {
   public:
    HierarchicalRTree(size_t max_node_capacity = 8) : max_capacity_(max_node_capacity) {
        root_ = std::make_shared<RTreeNode>();
    }

    // Insert a leaf Row Group entry into the R-Tree
    void insert(const RTreeEntry& entry);

    // Search hierarchical R-Tree subtrees across N dimensions and return matching candidate entries
    std::vector<RTreeEntry> search(const GenericMBR& query_mbr) const;

    // Search Level 2 Page MBRs for a matching query MBR across N dimensions
    std::vector<PageMBR> search_pages(const GenericMBR& query_mbr) const;

    // Root node accessors for serialization
    std::shared_ptr<RTreeNode> root() const { return root_; }
    void set_root(std::shared_ptr<RTreeNode> root) { root_ = std::move(root); }

    size_t total_leaf_entries() const;
    std::vector<RTreeEntry> all_leaf_entries() const;

   private:
    void search_node(const std::shared_ptr<RTreeNode>& node, const GenericMBR& query_mbr,
                     std::vector<RTreeEntry>& results) const;
    void insert_node(std::shared_ptr<RTreeNode>& node, const RTreeEntry& entry);
    void split_node(std::shared_ptr<RTreeNode>& node);

    std::shared_ptr<RTreeNode> root_;
    size_t max_capacity_{8};
};

template <typename T>
class TypedRTree {
   public:
    TypedRTree(size_t max_node_capacity = 8) : inner_(max_node_capacity) {}

    void insert_row_group(std::span<const T> records, uint32_t row_group_id, const std::string& filepath = "") {
        RTreeEntry entry;
        entry.mbr = extract_mbr(records);
        entry.row_group_id = row_group_id;
        entry.filepath = filepath;
        inner_.insert(entry);
    }

    std::vector<RTreeEntry> search(const GenericMBR& query_mbr) const { return inner_.search(query_mbr); }

    HierarchicalRTree& inner() { return inner_; }
    const HierarchicalRTree& inner() const { return inner_; }

   private:
    HierarchicalRTree inner_;
};

}  // namespace tick_db

#include "schemas/spatial_concept.h"
