#include "spatial/sidecar.h"

#include <fstream>
#include <iostream>
#include <vector>

namespace tick_db {

static constexpr uint32_t SIDECAR_MAGIC = 0x49445853;  // "SDXI"

bool SidecarIndex::write_index(const std::string& index_path, const HierarchicalRTree& rtree) {
    std::ofstream out(index_path, std::ios::binary);
    if (!out) {
        std::cerr << "[SidecarIndex] Failed to open index file for writing: " << index_path << "\n";
        return false;
    }

    out.write(reinterpret_cast<const char*>(&SIDECAR_MAGIC), sizeof(SIDECAR_MAGIC));

    auto all_entries = rtree.all_leaf_entries();
    uint32_t count = static_cast<uint32_t>(all_entries.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& entry : all_entries) {
        uint32_t dims = static_cast<uint32_t>(entry.mbr.dimensions());
        out.write(reinterpret_cast<const char*>(&dims), sizeof(dims));

        for (uint32_t d = 0; d < dims; ++d) {
            out.write(reinterpret_cast<const char*>(&entry.mbr.min_bounds[d]), sizeof(int64_t));
            out.write(reinterpret_cast<const char*>(&entry.mbr.max_bounds[d]), sizeof(int64_t));
        }

        out.write(reinterpret_cast<const char*>(&entry.row_group_id), sizeof(entry.row_group_id));

        uint32_t path_len = static_cast<uint32_t>(entry.filepath.size());
        out.write(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
        if (path_len > 0) {
            out.write(entry.filepath.data(), path_len);
        }

        // Level 2 Page MBRs serialization
        uint32_t num_pages = static_cast<uint32_t>(entry.page_mbrs.size());
        out.write(reinterpret_cast<const char*>(&num_pages), sizeof(num_pages));
        for (const auto& pm : entry.page_mbrs) {
            out.write(reinterpret_cast<const char*>(&pm.page_id), sizeof(pm.page_id));
            out.write(reinterpret_cast<const char*>(&pm.record_count), sizeof(pm.record_count));
            uint32_t pdims = static_cast<uint32_t>(pm.mbr.dimensions());
            out.write(reinterpret_cast<const char*>(&pdims), sizeof(pdims));
            for (uint32_t d = 0; d < pdims; ++d) {
                out.write(reinterpret_cast<const char*>(&pm.mbr.min_bounds[d]), sizeof(int64_t));
                out.write(reinterpret_cast<const char*>(&pm.mbr.max_bounds[d]), sizeof(int64_t));
            }
        }
    }

    return true;
}

bool SidecarIndex::read_index(const std::string& index_path, HierarchicalRTree& out_rtree) {
    std::ifstream in(index_path, std::ios::binary);
    if (!in) {
        std::cerr << "[SidecarIndex] Failed to open index file for reading: " << index_path << "\n";
        return false;
    }

    uint32_t magic = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != SIDECAR_MAGIC) {
        std::cerr << "[SidecarIndex] Invalid sidecar magic header in " << index_path << "\n";
        return false;
    }

    uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));

    HierarchicalRTree tree;
    for (uint32_t i = 0; i < count; ++i) {
        RTreeEntry entry;
        uint32_t dims = 0;
        in.read(reinterpret_cast<char*>(&dims), sizeof(dims));
        entry.mbr.min_bounds.resize(dims);
        entry.mbr.max_bounds.resize(dims);

        for (uint32_t d = 0; d < dims; ++d) {
            in.read(reinterpret_cast<char*>(&entry.mbr.min_bounds[d]), sizeof(int64_t));
            in.read(reinterpret_cast<char*>(&entry.mbr.max_bounds[d]), sizeof(int64_t));
        }

        in.read(reinterpret_cast<char*>(&entry.row_group_id), sizeof(entry.row_group_id));

        uint32_t path_len = 0;
        in.read(reinterpret_cast<char*>(&path_len), sizeof(path_len));
        if (path_len > 0) {
            entry.filepath.resize(path_len);
            in.read(&entry.filepath[0], path_len);
        }

        uint32_t num_pages = 0;
        if (in.read(reinterpret_cast<char*>(&num_pages), sizeof(num_pages))) {
            for (uint32_t p = 0; p < num_pages; ++p) {
                PageMBR pm;
                in.read(reinterpret_cast<char*>(&pm.page_id), sizeof(pm.page_id));
                in.read(reinterpret_cast<char*>(&pm.record_count), sizeof(pm.record_count));
                uint32_t pdims = 0;
                in.read(reinterpret_cast<char*>(&pdims), sizeof(pdims));
                pm.mbr.min_bounds.resize(pdims);
                pm.mbr.max_bounds.resize(pdims);
                for (uint32_t d = 0; d < pdims; ++d) {
                    in.read(reinterpret_cast<char*>(&pm.mbr.min_bounds[d]), sizeof(int64_t));
                    in.read(reinterpret_cast<char*>(&pm.mbr.max_bounds[d]), sizeof(int64_t));
                }
                entry.page_mbrs.push_back(pm);
            }
        }

        tree.insert(entry);
    }

    out_rtree = std::move(tree);
    return true;
}

}  // namespace tick_db
