#pragma once

#include <string>

#include "spatial/rtree.h"

namespace tick_db {

class SidecarIndex {
   public:
    SidecarIndex() = default;
    ~SidecarIndex() = default;

    // Serialize a Hierarchical R-Tree to a binary sidecar file (.index) or byte string
    static bool write_index(const std::string& index_path, const HierarchicalRTree& rtree);
    static std::string serialize_to_string(const HierarchicalRTree& rtree);

    // Read and deserialize a Hierarchical R-Tree from a file (.index / .parquet embedded metadata) or byte string
    static bool read_index(const std::string& index_path, HierarchicalRTree& out_rtree);
    static bool deserialize_from_string(const std::string& payload, HierarchicalRTree& out_rtree);
};

}  // namespace tick_db
