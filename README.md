# t	ick-db ⚡
> High-Performance, Time-Ascending Market Data & Time-Series Database engine built in Modern C++20 on Apache Parquet and Arrow C++, featuring Embedded Spatial R-Tree Indexing, Compressed Roaring Symbol Bitmaps, Zero-RAM Streaming Iterators, and Terminal SQL REPL Engine.

![Architecture](t	ick-db.png)

---

## 📌 Key Highlights & Architectural Principles

1. **Strict Time-Ascending Physical Layout (	s_exchange_ns)**:
   - Data inside Parquet files remains **100% time-ascending**. Unlike traditional databases that physically re-sort rows by symbol, 	ick-db preserves temporal integrity to enable sub-microsecond time-series range scans and hardware cache locality.
2. **Embedded Single-I/O Metadata Indexing (	ick_db.index.v1)**:
   - Spatial N-Dimensional R-Trees (MBR<N>) and compressed Roaring symbol bitmaps are stored **directly inside the Parquet FileMetaData footer**.
   - Opens files and extracts index structures in **1 single I/O call** without sidecar .index files or external lookups.
3. **Zero-Memory Streaming Iterators (RecordStream<T>)**:
   - Lazy page-by-page evaluation (O(1) RAM usage) that streams records without deserializing entire datasets into memory.
   - Micro-batch and multi-day range queries chain streams across daily partitioned folders seamlessly (MultiDayRecordStream<T>).
4. **Sub-Row-Group Symbol Range Slicing (1.0x Read Amplification)**:
   - Roaring symbol position bitmaps combined with Delta-Varint (LEB128) encoding and adaptive gap merging ($\text{max\_gap} = 4$) achieve 1.0x read amplification (0% wasted row decoding).
5. **Interactive SQL Terminal CLI (	ick_db_cli)**:
   - Fast REPL terminal engine executing queries like SELECT * FROM trades WHERE symbol = 'AAPL' AND ts_exchange_ns BETWEEN x AND y with microsecond-level query stats.
6. **CSV Historical Data Backfiller (	ick_db_ingest)**:
   - Ingests legacy CSV OHLCV / Tick data directly into embedded indexed Parquet structures.

---

## 🛠 Directory Layout & Storage Architecture

Data is stored hierarchically by **Schema Type** (rades, quotes, ook_levels, order_events, ohlcv) and partitioned by **Date** (YYYY-MM-DD):

`text
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
`

### Specifying Custom Storage Directories:
- **Terminal CLI Argument**: --db-path /path/to/custom_db
- **C++ API Config**:
  `cpp
  #include 
