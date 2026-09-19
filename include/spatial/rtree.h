#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "schemas/spatial_concept.h"
#include "spatial/mbr.h"

#include "catalog/symbol_index.h"

namespace tick_db {

using GenericMBR = DynamicMBR;

struct PageMBR {
    uint32_t page_id{0};
    DynamicMBR mbr;
    uint32_t record_count{0};
    uint64_t symbol_bitmask{0};
};

class HierarchicalRTree;

struct RTreeEntry {
    DynamicMBR mbr;
    uint32_t row_group_id{0};
    std::string filepath;
    uint64_t symbol_bitmask{0};
    std::vector<PageMBR> page_mbrs;
    std::shared_ptr<HierarchicalRTree> page_rtree;
};

struct RTreeNode {
    bool is_leaf{true};
    DynamicMBR mbr;
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
    std::vector<RTreeEntry> search(const DynamicMBR& query_mbr) const;

    // Search Level 2 Page MBRs for a matching query MBR across N dimensions
    std::vector<PageMBR> search_pages(const DynamicMBR& query_mbr) const;

    // Root node accessors for serialization
    std::shared_ptr<RTreeNode> root() const { return root_; }
    void set_root(std::shared_ptr<RTreeNode> root) { root_ = std::move(root); }

    size_t total_leaf_entries() const;
    std::vector<RTreeEntry> all_leaf_entries() const;

    SymbolIndexDirectory& symbol_directory() { return symbol_dir_; }
    const SymbolIndexDirectory& symbol_directory() const { return symbol_dir_; }

   private:
    void search_node(const std::shared_ptr<RTreeNode>& node, const DynamicMBR& query_mbr,
                     std::vector<RTreeEntry>& results) const;
    void insert_node(std::shared_ptr<RTreeNode>& node, const RTreeEntry& entry);
    void split_node(std::shared_ptr<RTreeNode>& node);

    std::shared_ptr<RTreeNode> root_;
    SymbolIndexDirectory symbol_dir_;
    size_t max_capacity_{8};
};

// Schema-Bound R-Tree Templated Directly Over Schema Class/Struct Type
template <SpatialRecord Schema, size_t MaxCapacity = 8>
class RTree {
   public:
    static constexpr size_t Dims = record_dimensions_v<Schema>;
    using MBRType = MBR<Dims>;

    RTree() : inner_(MaxCapacity) {}

    void insert_row_group(std::span<const Schema> records, uint32_t row_group_id, const std::string& filepath = "") {
        RTreeEntry entry;
        auto stack_mbr = extract_mbr(records);
        entry.mbr.min_bounds.assign(stack_mbr.min_bounds.begin(), stack_mbr.min_bounds.end());
        entry.mbr.max_bounds.assign(stack_mbr.max_bounds.begin(), stack_mbr.max_bounds.end());
        entry.row_group_id = row_group_id;
        entry.filepath = filepath;
        entry.symbol_bitmask = extract_symbol_bitmask(records);
        inner_.insert(entry);
        if (!records.empty()) {
            uint32_t current_sym = extract_primary_symbol_id(records[0]);
            uint32_t start_idx = 0;
            uint32_t count = 0;
            for (size_t i = 0; i < records.size(); ++i) {
                uint32_t sym_id = extract_primary_symbol_id(records[i]);
                if (sym_id == current_sym) {
                    count++;
                } else {
                    if (current_sym > 0 && count > 0) {
                        inner_.symbol_directory().add_symbol_range(current_sym, row_group_id, start_idx, count);
                    }
                    current_sym = sym_id;
                    start_idx = static_cast<uint32_t>(i);
                    count = 1;
                }
            }
            if (current_sym > 0 && count > 0) {
                inner_.symbol_directory().add_symbol_range(current_sym, row_group_id, start_idx, count);
            }
        }
    }

    std::vector<RTreeEntry> search(const MBRType& query_mbr) const {
        DynamicMBR dyn_query;
        dyn_query.min_bounds.assign(query_mbr.min_bounds.begin(), query_mbr.min_bounds.end());
        dyn_query.max_bounds.assign(query_mbr.max_bounds.begin(), query_mbr.max_bounds.end());
        return inner_.search(dyn_query);
    }

    std::vector<RTreeEntry> search(const DynamicMBR& query_mbr) const { return inner_.search(query_mbr); }

    HierarchicalRTree& inner() { return inner_; }
    const HierarchicalRTree& inner() const { return inner_; }

   private:
    HierarchicalRTree inner_;
};

template <typename Schema>
using TypedRTree = RTree<Schema>;

}  // namespace tick_db
