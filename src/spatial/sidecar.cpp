#include "spatial/sidecar.h"
#include "spatial/varint.h"
#include "storage/parquet_reader.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace tick_db {

static constexpr uint32_t SIDECAR_MAGIC = 0x49445853;  // "SDXI"

bool SidecarIndex::write_index(const std::string& index_path, const HierarchicalRTree& rtree) {
    std::ofstream out(index_path, std::ios::binary);
    if (!out) {
        std::cerr << "[SidecarIndex] Failed to open index file for writing: " << index_path << "\n";
        return false;
    }
    std::string payload = serialize_to_string(rtree);
    out.write(payload.data(), payload.size());
    return true;
}

std::string SidecarIndex::serialize_to_string(const HierarchicalRTree& rtree) {
    std::ostringstream out(std::ios::binary);

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
        out.write(reinterpret_cast<const char*>(&entry.symbol_bitmask), sizeof(entry.symbol_bitmask));

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

    // Serialize Inverted Symbol Index Directory
    const auto& sym_map = rtree.symbol_directory().map();
    uint32_t sym_count = static_cast<uint32_t>(sym_map.size());
    out.write(reinterpret_cast<const char*>(&sym_count), sizeof(sym_count));
    for (const auto& [sym_id, rgs] : sym_map) {
        out.write(reinterpret_cast<const char*>(&sym_id), sizeof(sym_id));
        uint32_t rg_count = static_cast<uint32_t>(rgs.size());
        out.write(reinterpret_cast<const char*>(&rg_count), sizeof(rg_count));
        if (rg_count > 0) {
            out.write(reinterpret_cast<const char*>(rgs.data()), rg_count * sizeof(uint32_t));
        }
    }

    // Serialize Sub-Row-Group Symbol Range Directory with Delta-Varint LEB128 Compression
    const auto& ranges_map = rtree.symbol_directory().ranges_map();
    uint32_t range_sym_count = static_cast<uint32_t>(ranges_map.size());
    out.write(reinterpret_cast<const char*>(&range_sym_count), sizeof(range_sym_count));

    for (const auto& [sym_id, ranges] : ranges_map) {
        out.write(reinterpret_cast<const char*>(&sym_id), sizeof(sym_id));
        uint32_t r_count = static_cast<uint32_t>(ranges.size());
        out.write(reinterpret_cast<const char*>(&r_count), sizeof(r_count));

        std::vector<uint8_t> varint_buf;
        uint32_t prev_start = 0;
        uint32_t prev_rg = 0;

        for (const auto& r : ranges) {
            uint32_t delta_rg = r.row_group_id - prev_rg;
            uint32_t delta_start = (r.row_group_id == prev_rg) ? (r.start_row - prev_start) : r.start_row;

            Varint::encode(delta_rg, varint_buf);
            Varint::encode(delta_start, varint_buf);
            Varint::encode(r.count, varint_buf);

            prev_rg = r.row_group_id;
            prev_start = r.start_row;
        }

        uint32_t compressed_bytes = static_cast<uint32_t>(varint_buf.size());
        out.write(reinterpret_cast<const char*>(&compressed_bytes), sizeof(compressed_bytes));
        if (compressed_bytes > 0) {
            out.write(reinterpret_cast<const char*>(varint_buf.data()), compressed_bytes);
        }
    }

    return out.str();
}

bool SidecarIndex::read_index(const std::string& index_path, HierarchicalRTree& out_rtree) {
    // 1. Try reading embedded index from Parquet KeyValueMetadata ("tick_db.index.v1")
    std::string embedded = ParquetReader::read_embedded_index(index_path);
    if (!embedded.empty()) {
        return deserialize_from_string(embedded, out_rtree);
    }

    // 2. Fallback to external .index sidecar file
    std::ifstream in(index_path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return deserialize_from_string(ss.str(), out_rtree);
}

bool SidecarIndex::deserialize_from_string(const std::string& payload, HierarchicalRTree& out_rtree) {
    if (payload.empty()) return false;

    std::istringstream in(payload, std::ios::binary);

    uint32_t magic = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != SIDECAR_MAGIC) {
        std::cerr << "[SidecarIndex] Invalid sidecar magic header in stream payload\n";
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
        in.read(reinterpret_cast<char*>(&entry.symbol_bitmask), sizeof(entry.symbol_bitmask));

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

    // Deserialize Inverted Symbol Index Directory
    uint32_t sym_count = 0;
    if (in.read(reinterpret_cast<char*>(&sym_count), sizeof(sym_count))) {
        for (uint32_t s = 0; s < sym_count; ++s) {
            uint32_t sym_id = 0;
            uint32_t rg_count = 0;
            in.read(reinterpret_cast<char*>(&sym_id), sizeof(sym_id));
            in.read(reinterpret_cast<char*>(&rg_count), sizeof(rg_count));
            std::vector<uint32_t> rgs(rg_count);
            if (rg_count > 0) {
                in.read(reinterpret_cast<char*>(rgs.data()), rg_count * sizeof(uint32_t));
            }
            for (uint32_t rg_id : rgs) {
                tree.symbol_directory().add_row_group(sym_id, rg_id);
            }
        }
    }

    // Deserialize Sub-Row-Group Symbol Range Directory with Delta-Varint LEB128 Decoding
    uint32_t range_sym_count = 0;
    if (in.read(reinterpret_cast<char*>(&range_sym_count), sizeof(range_sym_count))) {
        for (uint32_t s = 0; s < range_sym_count; ++s) {
            uint32_t sym_id = 0;
            uint32_t r_count = 0;
            in.read(reinterpret_cast<char*>(&sym_id), sizeof(sym_id));
            in.read(reinterpret_cast<char*>(&r_count), sizeof(r_count));

            uint32_t compressed_bytes = 0;
            in.read(reinterpret_cast<char*>(&compressed_bytes), sizeof(compressed_bytes));

            if (compressed_bytes > 0) {
                std::vector<uint8_t> varint_buf(compressed_bytes);
                in.read(reinterpret_cast<char*>(varint_buf.data()), compressed_bytes);

                const uint8_t* ptr = varint_buf.data();
                const uint8_t* end = ptr + compressed_bytes;

                uint32_t prev_rg = 0;
                uint32_t prev_start = 0;

                for (uint32_t r = 0; r < r_count; ++r) {
                    uint32_t delta_rg = Varint::decode(ptr, end);
                    uint32_t delta_start = Varint::decode(ptr, end);
                    uint32_t count_val = Varint::decode(ptr, end);

                    uint32_t curr_rg = prev_rg + delta_rg;
                    uint32_t curr_start = (curr_rg == prev_rg) ? (prev_start + delta_start) : delta_start;

                    tree.symbol_directory().add_symbol_range(sym_id, curr_rg, curr_start, count_val);

                    prev_rg = curr_rg;
                    prev_start = curr_start;
                }
            }
        }
    }

    out_rtree = std::move(tree);
    return true;
}

}  // namespace tick_db
