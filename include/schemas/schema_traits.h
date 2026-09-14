#pragma once

#include <span>
#include <vector>

#include "schemas/book_level.h"
#include "schemas/order_event.h"
#include "schemas/quote.h"
#include "schemas/trade.h"
#include "spatial/rtree.h"

namespace tick_db {

template <typename T>
struct SchemaTraits;

// 5D Trade Specialization: [ts_exchange_ns, seq_no, price, size, side]
template <>
struct SchemaTraits<Trade> {
    static constexpr size_t NUM_DIMENSIONS = 5;

    static GenericMBR extract_mbr(const Trade& record) {
        GenericMBR mbr;
        mbr.min_bounds = {static_cast<int64_t>(record.ts_exchange_ns), static_cast<int64_t>(record.seq_no),
                          record.price, record.size, static_cast<int64_t>(record.side)};
        mbr.max_bounds = mbr.min_bounds;
        return mbr;
    }

    static GenericMBR extract_mbr(std::span<const Trade> records) {
        if (records.empty()) return GenericMBR(NUM_DIMENSIONS);
        GenericMBR mbr = extract_mbr(records[0]);
        for (size_t i = 1; i < records.size(); ++i) {
            mbr.expand(extract_mbr(records[i]));
        }
        return mbr;
    }
};

// 6D Quote Specialization: [ts_exchange_ns, seq_no, bid_px, ask_px, bid_sz, ask_sz]
template <>
struct SchemaTraits<Quote> {
    static constexpr size_t NUM_DIMENSIONS = 6;

    static GenericMBR extract_mbr(const Quote& record) {
        GenericMBR mbr;
        mbr.min_bounds = {static_cast<int64_t>(record.ts_exchange_ns),
                          static_cast<int64_t>(record.seq_no),
                          record.bid_px,
                          record.ask_px,
                          record.bid_sz,
                          record.ask_sz};
        mbr.max_bounds = mbr.min_bounds;
        return mbr;
    }

    static GenericMBR extract_mbr(std::span<const Quote> records) {
        if (records.empty()) return GenericMBR(NUM_DIMENSIONS);
        GenericMBR mbr = extract_mbr(records[0]);
        for (size_t i = 1; i < records.size(); ++i) {
            mbr.expand(extract_mbr(records[i]));
        }
        return mbr;
    }
};

// 6D BookLevel Specialization: [ts_exchange_ns, seq_no, level_index, side, price, size]
template <>
struct SchemaTraits<BookLevel> {
    static constexpr size_t NUM_DIMENSIONS = 6;

    static GenericMBR extract_mbr(const BookLevel& record) {
        GenericMBR mbr;
        mbr.min_bounds = {static_cast<int64_t>(record.ts_exchange_ns),
                          static_cast<int64_t>(record.seq_no),
                          static_cast<int64_t>(record.level_index),
                          static_cast<int64_t>(record.side),
                          record.price,
                          record.size};
        mbr.max_bounds = mbr.min_bounds;
        return mbr;
    }

    static GenericMBR extract_mbr(std::span<const BookLevel> records) {
        if (records.empty()) return GenericMBR(NUM_DIMENSIONS);
        GenericMBR mbr = extract_mbr(records[0]);
        for (size_t i = 1; i < records.size(); ++i) {
            mbr.expand(extract_mbr(records[i]));
        }
        return mbr;
    }
};

// 7D OrderEvent Specialization: [ts_exchange_ns, seq_no, order_id, event_type, side, price, size]
template <>
struct SchemaTraits<OrderEvent> {
    static constexpr size_t NUM_DIMENSIONS = 7;

    static GenericMBR extract_mbr(const OrderEvent& record) {
        GenericMBR mbr;
        mbr.min_bounds = {static_cast<int64_t>(record.ts_exchange_ns),
                          static_cast<int64_t>(record.seq_no),
                          static_cast<int64_t>(record.order_id),
                          static_cast<int64_t>(record.event_type),
                          static_cast<int64_t>(record.side),
                          record.price,
                          record.size};
        mbr.max_bounds = mbr.min_bounds;
        return mbr;
    }

    static GenericMBR extract_mbr(std::span<const OrderEvent> records) {
        if (records.empty()) return GenericMBR(NUM_DIMENSIONS);
        GenericMBR mbr = extract_mbr(records[0]);
        for (size_t i = 1; i < records.size(); ++i) {
            mbr.expand(extract_mbr(records[i]));
        }
        return mbr;
    }
};

}  // namespace tick_db
