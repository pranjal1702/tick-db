import os
import json
import struct
import glob
import asyncio
from typing import List, Dict, Any, AsyncGenerator, Optional
from datetime import datetime
import pyarrow.parquet as pq

CLI_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../build/tick_db_cli"))

def get_default_db_path() -> str:
    env_path = os.environ.get("TICKDB_PATH")
    if env_path and os.path.exists(os.path.expanduser(env_path)):
        return os.path.expanduser(env_path)
    workspace_data = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../market_data"))
    if os.path.exists(workspace_data):
        return workspace_data
    home_tickdb = os.path.expanduser("~/tickDB")
    if os.path.exists(home_tickdb):
        return home_tickdb
    return workspace_data

DEFAULT_DB_PATH = get_default_db_path()

# Packed 56-byte binary packet layout for OHLCV:
# uint64(ts_ns), 8-byte char(symbol), int64(open), int64(high), int64(low), int64(close), int64(volume)
BINARY_PACKET_SIZE = 56
BINARY_FORMAT = "<Q8sqqqqq"

class TickDBClient:
    def __init__(self, db_path: Optional[str] = None, cli_path: str = CLI_PATH):
        self.db_path = os.path.expanduser(db_path) if db_path else get_default_db_path()
        self.cli_path = cli_path
        self._cached_symbols = None

    def get_metadata(self) -> Dict[str, Any]:
        """Discover available date partitions and top symbols."""
        date_folders = sorted(glob.glob(os.path.join(self.db_path, "date=*")))
        dates = [os.path.basename(d).replace("date=", "") for d in date_folders]
        
        symbols = self.get_symbols()
        
        return {
            "status": "connected",
            "db_path": self.db_path,
            "total_partitions": len(dates),
            "date_range": {
                "start": dates[0] if dates else None,
                "end": dates[-1] if dates else None
            },
            "dates": dates,
            "total_symbols": len(symbols),
            "symbols": symbols
        }

    def get_symbols(self) -> List[str]:
        """Extract unique symbols from a sample parquet file or return cached list."""
        if self._cached_symbols is not None:
            return self._cached_symbols
            
        sample_files = glob.glob(os.path.join(self.db_path, "date=*/ohlcv/data.parquet"))
        if not sample_files:
            return []
            
        try:
            # Read unique symbols from first day's partition
            table = pq.read_table(sample_files[0], columns=["symbol"])
            syms = sorted(table["symbol"].unique().to_pylist())
            self._cached_symbols = syms
            return syms
        except Exception:
            return ["INFY", "LTTS", "RELIANCE", "TCS", "HDFCBANK", "ICICIBANK"]

    @staticmethod
    def determine_auto_timeframe(raw_count: int) -> str:
        """Pick optimal candlestick timeframe based on actual bar count to maintain 200-800 candles on chart."""
        if raw_count <= 600:
            return "1m"
        elif raw_count <= 2500:
            return "5m"
        elif raw_count <= 7500:
            return "15m"
        elif raw_count <= 25000:
            return "1h"
        else:
            return "1d"

    async def query_ohlcv(
        self,
        symbol: str,
        start_time: str,
        end_time: str,
        price_filter: Optional[str] = None,
        timeframe: str = "auto"
    ) -> List[Dict[str, Any]]:
        """Fetch OHLCV candles as a list of dicts, optionally resampled."""
        raw_bars = []
        async for item in self.stream_ohlcv_json([symbol], start_time, end_time, price_filter):
            raw_bars.append(item)

        if not raw_bars:
            return []

        resolved_tf = timeframe
        if timeframe == "auto":
            resolved_tf = self.determine_auto_timeframe(len(raw_bars))

        if resolved_tf == "1m":
            for b in raw_bars:
                b["timeframe"] = "1m"
            return raw_bars

        def get_bucket_key(ts_ns: int, time_str: str) -> str:
            if resolved_tf == "1d":
                return time_str.split(":")[0] if ":" in time_str else str(ts_ns // 86400_000_000_000)
            elif resolved_tf == "1h":
                return str(ts_ns // (3600 * 1_000_000_000))
            elif resolved_tf == "15m":
                return str(ts_ns // (900 * 1_000_000_000))
            elif resolved_tf == "5m":
                return str(ts_ns // (300 * 1_000_000_000))
            return str(ts_ns)

        records = []
        current_bucket_key = None
        current_candle = None

        for bar in raw_bars:
            b_key = get_bucket_key(bar.get("timestamp_ns", 0), bar.get("time", ""))
            if b_key != current_bucket_key:
                if current_candle is not None:
                    records.append(current_candle)
                current_bucket_key = b_key
                current_candle = {
                    "timestamp_ns": bar["timestamp_ns"],
                    "time": bar["time"],
                    "symbol": bar["symbol"],
                    "open": bar["open"],
                    "high": bar["high"],
                    "low": bar["low"],
                    "close": bar["close"],
                    "price": bar["close"],
                    "volume": bar["volume"],
                    "timeframe": resolved_tf
                }
            else:
                if bar["high"] > current_candle["high"]:
                    current_candle["high"] = bar["high"]
                if bar["low"] < current_candle["low"]:
                    current_candle["low"] = bar["low"]
                current_candle["close"] = bar["close"]
                current_candle["price"] = bar["close"]
                current_candle["volume"] += bar["volume"]

        if current_candle is not None:
            records.append(current_candle)

        return records

    async def stream_ohlcv_json(
        self,
        symbols: List[str],
        start_time: str,
        end_time: str,
        price_filter: Optional[str] = None
    ) -> AsyncGenerator[Dict[str, Any], None]:
        """Stream OHLCV records via JSON lines from tick_db_cli."""
        sym_clause = ""
        if len(symbols) == 1:
            sym_clause = f"AND symbol = '{symbols[0]}'"
        elif len(symbols) > 1:
            sym_list = ", ".join(f"'{s}'" for s in symbols)
            sym_clause = f"AND symbol IN ({sym_list})"

        px_clause = f"AND {price_filter}" if price_filter else ""
        sql = f"SELECT * FROM ohlcv WHERE ts_exchange_ns BETWEEN {start_time} AND {end_time} {sym_clause} {px_clause};"

        cmd = [
            self.cli_path,
            "--db-path", self.db_path,
            "--json-stream",
            "--sql", sql
        ]

        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )

        while True:
            line = await proc.stdout.readline()
            if not line:
                break
            line_str = line.decode("utf-8").strip()
            if not line_str or not line_str.startswith("{"):
                continue
            try:
                data = json.loads(line_str)
                # Ensure open, high, low, close, volume are present
                if "open" not in data and "price" in data:
                    data["open"] = data["price"]
                    data["high"] = data["price"]
                    data["low"] = data["price"]
                    data["close"] = data["price"]
                yield data
            except json.JSONDecodeError:
                continue

        await proc.wait()

    async def stream_ohlcv_binary(
        self,
        symbols: List[str],
        start_time: str,
        end_time: str
    ) -> AsyncGenerator[bytes, None]:
        """Stream raw packed 56-byte binary chunks directly for ultra-low-latency WebSockets."""
        sym_clause = ""
        if len(symbols) == 1:
            sym_clause = f"AND symbol = '{symbols[0]}'"
        elif len(symbols) > 1:
            sym_list = ", ".join(f"'{s}'" for s in symbols)
            sym_clause = f"AND symbol IN ({sym_list})"

        sql = f"SELECT * FROM ohlcv WHERE ts_exchange_ns BETWEEN {start_time} AND {end_time} {sym_clause};"

        cmd = [
            self.cli_path,
            "--db-path", self.db_path,
            "--binary-stream",
            "--sql", sql
        ]

        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )

        CHUNK_SIZE = BINARY_PACKET_SIZE * 100  # Stream 100 records per socket frame
        while True:
            chunk = await proc.stdout.read(CHUNK_SIZE)
            if not chunk:
                break
            yield chunk

        await proc.wait()
