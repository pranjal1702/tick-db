# tick-db ⚡
> High-Performance, Time-Ascending Market Data & Time-Series Database engine built in Modern C++20 on Apache Parquet and Arrow C++, featuring Embedded Spatial R-Tree Indexing, Compressed Roaring Symbol Bitmaps, Zero-RAM Streaming Iterators, and Terminal SQL REPL Engine.

![Architecture](tick-db.png)

---

## 📌 Key Highlights & Architectural Principles

1. **Strict Time-Ascending Physical Layout (`ts_exchange_ns`)**:
   - Data inside Parquet files remains **100% time-ascending**. Unlike traditional databases that physically re-sort rows by symbol, `tick-db` preserves temporal integrity to enable sub-microsecond time-series range scans and hardware cache locality.
2. **Embedded Single-I/O Metadata Indexing (`tick_db.index.v1`)**:
   - Spatial N-Dimensional R-Trees (`MBR<N>`) and compressed Roaring symbol bitmaps are stored **directly inside the Parquet FileMetaData footer**.
   - Opens files and extracts index structures in **1 single I/O call** without sidecar `.index` files or external lookups.
3. **Zero-Memory Streaming Iterators (`RecordStream<T>`)**:
   - Lazy page-by-page evaluation ($O(1)$ RAM usage) that streams records without deserializing entire datasets into memory.
   - Micro-batch and multi-day range queries chain streams across daily partitioned folders seamlessly (`MultiDayRecordStream<T>`).
4. **Sub-Row-Group Symbol Range Slicing (1.0x Read Amplification)**:
   - Roaring symbol position bitmaps combined with Delta-Varint (LEB128) encoding and adaptive gap merging (max_gap = 4) achieve 1.0x read amplification (0% wasted row decoding).
5. **Interactive SQL Terminal CLI (`tick_db_cli`)**:
   - Fast REPL terminal engine executing queries like `SELECT * FROM trades WHERE symbol = 'AAPL' AND ts_exchange_ns BETWEEN x AND y` with microsecond-level query stats.
6. **CSV Historical Data Backfiller (`tick_db_ingest`)**:
   - Ingests legacy CSV OHLCV / Tick data directly into embedded indexed Parquet structures.

---

## 🛠 Directory Layout & Storage Architecture

Data is stored hierarchically by **Schema Type** (`trades`, `quotes`, `book_levels`, `order_events`, `ohlcv`) and partitioned by **Date** (`YYYY-MM-DD`):

```text
<data_directory>/
├── catalog/
│   └── symbol_catalog.json           # Thread-safe global symbol dictionary mapping ID <-> String
├── trades/
│   ├── 2026-09-18/
│   │   └── data.parquet             # Parquet storage containing embedded spatial R-Tree index
│   └── 2026-09-19/
│       └── data.parquet
├── ohlcv/
│   └── 2026-09-19/
│       └── data.parquet
└── quotes/
    └── 2026-09-19/
        └── data.parquet
```

### Specifying Custom Storage Directories:
- **Terminal CLI Argument**: `--db-path /path/to/custom_db`
- **C++ API Config**:
  ```cpp
  #include "catalog/database_config.h"
  DatabaseConfig::get_instance().set_data_directory("/var/lib/tick_db_data");
  ```

---

## 🚀 Quickstart & Building

### Prerequisites
- GCC 11+ or Clang 13+ (C++20 support required)
- CMake 3.16+
- Apache Arrow & Parquet C++ libraries (`libarrow-dev`, `libparquet-dev`)

### Build Steps

```bash
# Clone repository
git clone https://github.com/pranjal1702/tick-db.git
cd tick-db

# Create build directory
mkdir -p build && cd build

# Configure and compile
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
```

Target binaries compiled in `build/`:
- `tick_db_cli`: Interactive terminal SQL REPL & single query execution engine.
- `tick_db_ingest`: CSV backfilling ingestion utility.
- `tick_db_test`: Complete 10-part automated test suite & benchmark.

---

## 💻 How to Use

### 1. Interactive SQL Terminal REPL (`tick_db_cli`)

Launch the terminal REPL shell to execute fast queries:

```bash
./tick_db_cli --db-path ./db_data
```

```sql
tick_db> SELECT * FROM trades WHERE symbol = 'AAPL' AND ts_exchange_ns BETWEEN 1700000000000000000 AND 1700003600000000000
tick_db> SELECT * FROM ohlcv WHERE symbol = 'NVDA' AND price >= 45000000000
tick_db> exit
```

#### Run a Single SQL Query directly from bash:
```bash
./tick_db_cli --db-path ./db_data --sql "SELECT * FROM trades WHERE symbol = 'AAPL' AND price >= 150000000"
```

### 2. CSV Historical Backfiller (`tick_db_ingest`)

Backfill OHLCV or Tick data from CSV files into indexed Parquet format:

```bash
./tick_db_ingest --csv data/ohlcv_sample.csv --schema ohlcv --date 2026-09-19 --db-path ./db_data
```

### 3. Using the C++ API

```cpp
#include "catalog/symbol_catalog.h"
#include "catalog/database_config.h"
#include "query/query_engine.h"
#include "schemas/ohlcv.h"

int main() {
    // 1. Configure DB path
    DatabaseConfig::get_instance().set_data_directory("./db_data");

    // 2. Multi-day range query execution stream
    uint32_t symbol_id = SymbolCatalog::get_instance().get_or_create_id("AAPL");
    uint64_t start_ns = 1700000000000000000ULL;
    uint64_t end_ns   = 1700086400000000000ULL;

    std::vector<std::string> dates = {"2026-09-18", "2026-09-19"};

    auto stream = QueryEngine::execute_multi_day_stream<OhlcvRecord>(
        "ohlcv", dates, symbol_id, start_ns, end_ns
    );

    // 3. O(1) RAM streaming iteration
    OhlcvRecord record;
    size_t count = 0;
    while (stream.next(record)) {
        count++;
        // Process record without high memory overhead
    }

    return 0;
}
```

---

## 📊 Benchmark & Implementation Verification Results

The database includes a comprehensive automated test suite (`tick_db_test`) validating indexing precision, latency, and read amplification:

| Test ID | Architectural Target | Verified Feature | Performance Metric / Status |
| :--- | :--- | :--- | :--- |
| **ND-1** | 5D Trade Indexing | 5D Spatial MBR Querying (`ts`, `price`, `size`, `sym`, `side`) | **PASSED** (Exact 41 records pruned) |
| **ND-2** | 7D OrderEvent L3 | 7D Order Lifecycle MBR Pruning | **PASSED** (0 false positives) |
| **ND-3** | Symbol Auto-Hash | String symbol fast hash pruning (`AAPL` vs `MSFT`) | **PASSED** (Sub-microsecond resolution) |
| **ND-4** | Shared Multi-Symbol | Single Parquet file storing multiple symbols | **PASSED** (Correct bitmask separation) |
| **ND-5** | Zero Read Amplification | Sub-Row-Group Symbol Position Range Slicing | **1.0x Read Amplification** (500/1000 records examined) |
| **ND-6** | Position Compression | Delta-Varint (LEB128) & Adaptive Gap Merging | **500 ranges -> 1 span** (LEB128 1-2 bytes per range) |
| **ND-7** | Single I/O Metadata Index | Parquet `KeyValueMetadata` footer decoding & `RecordStream<T>` | **1 I/O Call** ($O(1)$ RAM streaming) |
| **ND-8** | Multi-Day Range Query | Chained multi-day time-ascending iteration across folders | **PASSED** (Strict time order maintained) |
| **ND-9** | SQL Query Engine | Lexer, AST Parser, and Query Planner execution | **PASSED** (Sub-millisecond terminal response) |
| **ND-10**| CSV Ingestion | End-to-end CSV backfill & index synthesis | **PASSED** (100% data integrity verified) |

### Test Suite Execution Output
```text
=======================================================
 Running Generic N-Dimensional R-Tree Architecture Tests
=======================================================
[TEST ND-1] Testing 5-Dimensional Schema-Driven Trade R-Tree Indexing... PASSED
[TEST ND-2] Testing 7-Dimensional Schema-Driven OrderEvent L3 R-Tree Indexing... PASSED
[TEST ND-3] Testing String Symbol Auto-Hash R-Tree Indexing... PASSED
[TEST ND-4] Testing Multi-Symbol Shared Parquet & Symbol Bitmask Indexing... PASSED
[TEST ND-5] Testing Sub-Row-Group Symbol Range Pruning (Zero Read Amplification)... PASSED
[TEST ND-6] Testing Delta-Varint Position Index Compression & Gap Merging... PASSED
[TEST ND-7] Testing Embedded Parquet Metadata Indexing & RecordStream<T> Iterator... PASSED
[TEST ND-8] Testing Multi-Day Range Querying & Chained Stream Iteration... PASSED
[TEST ND-9] Testing SQL Query Engine Parsing & Streaming Execution... PASSED
[TEST ND-10] Testing CSV Backfill Ingestion & Embedded Parquet Generation... PASSED
=======================================================
 ALL GENERIC N-DIMENSIONAL R-TREE TESTS PASSED! 
=======================================================
```

---

## 🔬 Core Implementation Deep Dive

```text
                          +---------------------------------------+
                          |   SQL Query String / C++ API Request  |
                          +---------------------------------------+
                                              |
                                              v
                          +---------------------------------------+
                          |       SQL Engine / Query Planner       |
                          +---------------------------------------+
                                              |
                                              v
                          +---------------------------------------+
                          |   1 I/O FileMetaData Footer Read      |
                          |   Extract 'tick_db.index.v1' Payload  |
                          +---------------------------------------+
                                              |
                                              v
                          +---------------------------------------+
                          |      Spatial R-Tree MBR Pruning       |
                          |   + LEB128 Delta-Varint Gap Slicing   |
                          +---------------------------------------+
                                              |
                                              v
                          +---------------------------------------+
                          |    RecordStream<T> Zero-RAM Iterator  |
                          |      (Lazy Page-by-Page Streaming)    |
                          +---------------------------------------+
```

### 1. Spatial MBR Bounding Boxes (`MBR<N>`)
- Every record is mapped to an N-dimensional Minimum Bounding Rectangle using 64-bit integer fixed-point coordinates (`ts_exchange_ns`, `price`, `size`, `symbol_id`, etc.).
- Spatial R-Tree nodes eliminate non-matching row groups before decoding Arrow record batches.

### 2. LEB128 Varint Delta Compression
- Symbol record position offsets inside Parquet files are delta-encoded using LEB128 variable-byte encoding, reducing index size by 75-80%.

### 3. Embedded Parquet KeyValueMetadata
- Index serialization uses zero external `.index` sidecars. The binary payload is stored in the standard Parquet `FileMetaData` footer key `tick_db.index.v1`.
- When opened, the index is parsed in **one single I/O read**, avoiding multiple disk seeks.

---

## 📜 License
MIT License. Created & maintained for ultra-low latency quantitative trading & market data analytics.
