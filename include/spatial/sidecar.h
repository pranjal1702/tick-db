#pragma once

#include <string>

#include "spatial/rtree.h"

namespace tick_db {

class SidecarIndex {
   public:
    SidecarIndex() = default;
    ~SidecarIndex() = default;

    // Serialize a Hierarchical R-Tree to a binary sidecar file (.index)
    static bool write_index(const std::string& index_path, const HierarchicalRTree& rtree);

    // Read and deserialize a Hierarchical R-Tree from a binary sidecar file (.index)
    static bool read_index(const std::string& index_path, HierarchicalRTree& out_rtree);
};

}  // namespace tick_db
