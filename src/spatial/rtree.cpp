#include "spatial/rtree.h"

namespace tick_db {

void HierarchicalRTree::insert(const RTreeEntry& entry) {
    RTreeEntry entry_copy = entry;
    // Automatically construct Level 2 Page R-Tree if page count > 32
    if (entry_copy.page_mbrs.size() > 32 && !entry_copy.page_rtree) {
        entry_copy.page_rtree = std::make_shared<HierarchicalRTree>(8);
        for (const auto& pmbr : entry_copy.page_mbrs) {
            RTreeEntry pentry;
            pentry.mbr = pmbr.mbr;
            pentry.row_group_id = pmbr.page_id;
            entry_copy.page_rtree->insert(pentry);
        }
    }
    insert_node(root_, entry_copy);
}

std::vector<RTreeEntry> HierarchicalRTree::search(const GenericMBR& query_mbr) const {
    std::vector<RTreeEntry> results;
    if (root_) {
        search_node(root_, query_mbr, results);
    }
    return results;
}

std::vector<PageMBR> HierarchicalRTree::search_pages(const GenericMBR& query_mbr) const {
    std::vector<PageMBR> page_results;
    auto rgs = search(query_mbr);

    for (const auto& rg : rgs) {
        if (rg.page_rtree) {
            auto pmatches = rg.page_rtree->search(query_mbr);
            for (const auto& pm : pmatches) {
                page_results.push_back({pm.row_group_id, pm.mbr, 0});
            }
        } else {
            for (const auto& pm : rg.page_mbrs) {
                if (pm.mbr.intersects(query_mbr)) {
                    page_results.push_back(pm);
                }
            }
        }
    }

    return page_results;
}

void HierarchicalRTree::search_node(const std::shared_ptr<RTreeNode>& node, const GenericMBR& query_mbr,
                                    std::vector<RTreeEntry>& results) const {
    if (!node || !node->mbr.intersects(query_mbr)) {
        return;  // Subtree pruned on N-dimensions!
    }

    if (node->is_leaf) {
        for (const auto& entry : node->entries) {
            if (entry.mbr.intersects(query_mbr)) {
                results.push_back(entry);
            }
        }
    } else {
        for (const auto& child : node->children) {
            search_node(child, query_mbr, results);
        }
    }
}

void HierarchicalRTree::insert_node(std::shared_ptr<RTreeNode>& node, const RTreeEntry& entry) {
    node->mbr.expand(entry.mbr);

    if (node->is_leaf) {
        node->entries.push_back(entry);
        if (node->entries.size() > max_capacity_) {
            split_node(node);
        }
    } else {
        size_t best_child = 0;
        int64_t min_expansion = std::numeric_limits<int64_t>::max();

        for (size_t i = 0; i < node->children.size(); ++i) {
            GenericMBR temp = node->children[i]->mbr;
            temp.expand(entry.mbr);
            int64_t exp = 0;
            if (!temp.min_bounds.empty()) {
                exp = (temp.max_bounds[0] - temp.min_bounds[0]);
            }
            if (exp < min_expansion) {
                min_expansion = exp;
                best_child = i;
            }
        }
        insert_node(node->children[best_child], entry);
    }
}

void HierarchicalRTree::split_node(std::shared_ptr<RTreeNode>& node) {
    if (node->entries.size() <= max_capacity_) return;

    auto child1 = std::make_shared<RTreeNode>();
    auto child2 = std::make_shared<RTreeNode>();
    child1->is_leaf = true;
    child2->is_leaf = true;

    size_t mid = node->entries.size() / 2;
    for (size_t i = 0; i < mid; ++i) {
        child1->entries.push_back(node->entries[i]);
        child1->mbr.expand(node->entries[i].mbr);
    }
    for (size_t i = mid; i < node->entries.size(); ++i) {
        child2->entries.push_back(node->entries[i]);
        child2->mbr.expand(node->entries[i].mbr);
    }

    node->entries.clear();
    node->is_leaf = false;
    node->children.push_back(child1);
    node->children.push_back(child2);
}

size_t HierarchicalRTree::total_leaf_entries() const {
    size_t count = 0;
    std::vector<std::shared_ptr<RTreeNode>> stack = {root_};
    while (!stack.empty()) {
        auto top = stack.back();
        stack.pop_back();
        if (top) {
            if (top->is_leaf) {
                count += top->entries.size();
            } else {
                for (const auto& c : top->children) stack.push_back(c);
            }
        }
    }
    return count;
}

std::vector<RTreeEntry> HierarchicalRTree::all_leaf_entries() const {
    std::vector<RTreeEntry> results;
    std::vector<std::shared_ptr<RTreeNode>> stack = {root_};
    while (!stack.empty()) {
        auto top = stack.back();
        stack.pop_back();
        if (top) {
            if (top->is_leaf) {
                for (const auto& e : top->entries) results.push_back(e);
            } else {
                for (const auto& c : top->children) stack.push_back(c);
            }
        }
    }
    return results;
}

}  // namespace tick_db
