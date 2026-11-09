#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "query/record_stream.h"
#include "schemas/base.h"
#include "schemas/book_level.h"
#include "schemas/order_event.h"

namespace tick_db {

struct LevelSnapshot {
    int64_t price{0};
    int64_t size{0};
    uint32_t order_count{0};
};

struct DepthSnapshot {
    uint64_t timestamp_ns{0};
    uint64_t seq_no{0};
    std::vector<LevelSnapshot> bids;
    std::vector<LevelSnapshot> asks;
};

// High-performance active Limit Order Book (LOB) reconstruction engine
class OrderBook {
   public:
    OrderBook() = default;
    explicit OrderBook(std::string symbol) : symbol_(std::move(symbol)) {}

    void clear() {
        bids_.clear();
        asks_.clear();
        orders_.clear();
        last_ts_ = 0;
        last_seq_ = 0;
    }

    // -------------------------------------------------------------
    // L2 Depth Update (BookLevel)
    // -------------------------------------------------------------
    void apply_book_level(const BookLevel& level) {
        last_ts_ = level.ts_exchange_ns;
        last_seq_ = level.seq_no;

        if (level.side == Side::Bid) {
            if (level.size <= 0) {
                bids_.erase(level.price);
            } else {
                bids_[level.price] = {level.price, level.size, 1};
            }
        } else if (level.side == Side::Ask) {
            if (level.size <= 0) {
                asks_.erase(level.price);
            } else {
                asks_[level.price] = {level.price, level.size, 1};
            }
        }
    }

    void apply_book_levels(std::span<const BookLevel> levels) {
        for (const auto& lvl : levels) {
            apply_book_level(lvl);
        }
    }

    // -------------------------------------------------------------
    // L3 Full Order Lifecycle Update (OrderEvent)
    // -------------------------------------------------------------
    struct StoredOrder {
        uint64_t order_id;
        Side side;
        int64_t price;
        int64_t size;
    };

    void apply_order_event(const OrderEvent& ev) {
        last_ts_ = ev.ts_exchange_ns;
        last_seq_ = ev.seq_no;

        switch (ev.event_type) {
            case OrderEventType::Add: {
                orders_[ev.order_id] = {ev.order_id, ev.side, ev.price, ev.size};
                if (ev.side == Side::Bid) {
                    auto& lvl = bids_[ev.price];
                    lvl.price = ev.price;
                    lvl.size += ev.size;
                    lvl.order_count++;
                } else if (ev.side == Side::Ask) {
                    auto& lvl = asks_[ev.price];
                    lvl.price = ev.price;
                    lvl.size += ev.size;
                    lvl.order_count++;
                }
                break;
            }
            case OrderEventType::Modify: {
                auto it = orders_.find(ev.order_id);
                if (it != orders_.end()) {
                    // Remove old size from old price level
                    remove_order_from_level(it->second);
                    // Update order details
                    it->second.price = ev.price;
                    it->second.size = ev.size;
                    // Add new size to new price level
                    add_order_to_level(it->second);
                }
                break;
            }
            case OrderEventType::Cancel: {
                auto it = orders_.find(ev.order_id);
                if (it != orders_.end()) {
                    int64_t cancel_qty = std::min(ev.size, it->second.size);
                    reduce_order_level(it->second.side, it->second.price, cancel_qty);
                    it->second.size -= cancel_qty;
                    if (it->second.size <= 0) {
                        orders_.erase(it);
                    }
                }
                break;
            }
            case OrderEventType::Execute: {
                auto it = orders_.find(ev.order_id);
                if (it != orders_.end()) {
                    int64_t fill_qty = std::min(ev.size, it->second.size);
                    reduce_order_level(it->second.side, it->second.price, fill_qty);
                    it->second.size -= fill_qty;
                    if (it->second.size <= 0) {
                        orders_.erase(it);
                    }
                }
                break;
            }
        }
    }

    void apply_order_events(std::span<const OrderEvent> events) {
        for (const auto& ev : events) {
            apply_order_event(ev);
        }
    }

    // -------------------------------------------------------------
    // Best Bid & Offer (BBO) & Spread
    // -------------------------------------------------------------
    bool has_bid() const { return !bids_.empty(); }
    bool has_ask() const { return !asks_.empty(); }
    bool is_valid_bbo() const { return has_bid() && has_ask() && best_bid_price() <= best_ask_price(); }

    int64_t best_bid_price() const {
        return bids_.empty() ? 0 : bids_.begin()->first;
    }

    int64_t best_bid_size() const {
        return bids_.empty() ? 0 : bids_.begin()->second.size;
    }

    int64_t best_ask_price() const {
        return asks_.empty() ? 0 : asks_.begin()->first;
    }

    int64_t best_ask_size() const {
        return asks_.empty() ? 0 : asks_.begin()->second.size;
    }

    int64_t spread() const {
        if (!has_bid() || !has_ask()) return 0;
        return best_ask_price() - best_bid_price();
    }

    double mid_price() const {
        if (!has_bid() || !has_ask()) return 0.0;
        return (best_bid_price() + best_ask_price()) / 2.0;
    }

    // Volume-Weighted Mid-Price (Microprice):
    // Microprice = (AskSize * BidPrice + BidSize * AskPrice) / (BidSize + AskSize)
    double microprice() const {
        if (!has_bid() || !has_ask()) return 0.0;
        int64_t bid_px = best_bid_price();
        int64_t ask_px = best_ask_price();
        int64_t bid_sz = best_bid_size();
        int64_t ask_sz = best_ask_size();
        int64_t total_sz = bid_sz + ask_sz;
        if (total_sz <= 0) return mid_price();
        return static_cast<double>(ask_sz * bid_px + bid_sz * ask_px) / static_cast<double>(total_sz);
    }

    // Order Book Imbalance (OBI) at top N levels:
    // OBI = (Sum(BidSize) - Sum(AskSize)) / (Sum(BidSize) + Sum(AskSize))
    // Yields values in [-1.0, 1.0].
    double order_book_imbalance(size_t max_depth = 5) const {
        int64_t bid_vol = 0;
        size_t count = 0;
        for (auto it = bids_.begin(); it != bids_.end() && count < max_depth; ++it, ++count) {
            bid_vol += it->second.size;
        }

        int64_t ask_vol = 0;
        count = 0;
        for (auto it = asks_.begin(); it != asks_.end() && count < max_depth; ++it, ++count) {
            ask_vol += it->second.size;
        }

        int64_t total_vol = bid_vol + ask_vol;
        if (total_vol == 0) return 0.0;
        return static_cast<double>(bid_vol - ask_vol) / static_cast<double>(total_vol);
    }

    // Volume-Weighted Average Price (VWAP) for executing target quantity against order book depth
    double vwap(Side side, int64_t target_volume) const {
        if (target_volume <= 0) return 0.0;
        int64_t remaining = target_volume;
        double accumulated_cost = 0.0;

        if (side == Side::Ask || side == Side::None) {
            // Buying from asks
            for (const auto& [px, lvl] : asks_) {
                int64_t take_qty = std::min(remaining, lvl.size);
                accumulated_cost += take_qty * static_cast<double>(px);
                remaining -= take_qty;
                if (remaining <= 0) break;
            }
        } else {
            // Selling into bids
            for (const auto& [px, lvl] : bids_) {
                int64_t take_qty = std::min(remaining, lvl.size);
                accumulated_cost += take_qty * static_cast<double>(px);
                remaining -= take_qty;
                if (remaining <= 0) break;
            }
        }

        int64_t executed = target_volume - remaining;
        if (executed <= 0) return 0.0;
        return accumulated_cost / static_cast<double>(executed);
    }

    DepthSnapshot get_depth_snapshot(size_t max_levels = 10) const {
        DepthSnapshot snap;
        snap.timestamp_ns = last_ts_;
        snap.seq_no = last_seq_;

        size_t count = 0;
        for (auto it = bids_.begin(); it != bids_.end() && count < max_levels; ++it, ++count) {
            snap.bids.push_back(it->second);
        }

        count = 0;
        for (auto it = asks_.begin(); it != asks_.end() && count < max_levels; ++it, ++count) {
            snap.asks.push_back(it->second);
        }

        return snap;
    }

    size_t bid_depth() const { return bids_.size(); }
    size_t ask_depth() const { return asks_.size(); }
    size_t total_active_orders() const { return orders_.size(); }
    uint64_t last_timestamp_ns() const { return last_ts_; }
    uint64_t last_seq_no() const { return last_seq_; }

   private:
    void add_order_to_level(const StoredOrder& order) {
        if (order.side == Side::Bid) {
            auto& lvl = bids_[order.price];
            lvl.price = order.price;
            lvl.size += order.size;
            lvl.order_count++;
        } else {
            auto& lvl = asks_[order.price];
            lvl.price = order.price;
            lvl.size += order.size;
            lvl.order_count++;
        }
    }

    void remove_order_from_level(const StoredOrder& order) {
        if (order.side == Side::Bid) {
            auto it = bids_.find(order.price);
            if (it != bids_.end()) {
                it->second.size -= order.size;
                if (it->second.order_count > 0) it->second.order_count--;
                if (it->second.size <= 0) bids_.erase(it);
            }
        } else {
            auto it = asks_.find(order.price);
            if (it != asks_.end()) {
                it->second.size -= order.size;
                if (it->second.order_count > 0) it->second.order_count--;
                if (it->second.size <= 0) asks_.erase(it);
            }
        }
    }

    void reduce_order_level(Side side, int64_t price, int64_t qty) {
        if (side == Side::Bid) {
            auto it = bids_.find(price);
            if (it != bids_.end()) {
                it->second.size -= qty;
                if (it->second.size <= 0) bids_.erase(it);
            }
        } else {
            auto it = asks_.find(price);
            if (it != asks_.end()) {
                it->second.size -= qty;
                if (it->second.size <= 0) asks_.erase(it);
            }
        }
    }

    std::string symbol_;
    uint64_t last_ts_{0};
    uint64_t last_seq_{0};

    // Bids sorted highest to lowest price
    std::map<int64_t, LevelSnapshot, std::greater<int64_t>> bids_;
    // Asks sorted lowest to highest price
    std::map<int64_t, LevelSnapshot, std::less<int64_t>> asks_;
    // L3 individual order map: order_id -> StoredOrder
    std::unordered_map<uint64_t, StoredOrder> orders_;
};

}  // namespace tick_db

