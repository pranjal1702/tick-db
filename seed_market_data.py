#!/usr/bin/env python3
import os
import random
import datetime
import subprocess

DB_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "market_data"))
BUILD_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "build"))
INGEST_BIN = os.path.join(BUILD_DIR, "tick_db_ingest")

# 20 trading days across Jan 2023 (Mondays through Fridays)
DATES = []
cur_date = datetime.date(2023, 1, 2)
while len(DATES) < 20:
    if cur_date.weekday() < 5:  # Monday to Friday
        DATES.append(cur_date.strftime("%Y-%m-%d"))
    cur_date += datetime.timedelta(days=1)

SYMBOLS = {
    "INFY": {"base": 1508.0, "vol": 0.0006},
    "LTTS": {"base": 3740.0, "vol": 0.0008},
    "RELIANCE": {"base": 2540.0, "vol": 0.0005},
    "TCS": {"base": 3260.0, "vol": 0.0005},
    "HDFCBANK": {"base": 1620.0, "vol": 0.0004},
    "ICICIBANK": {"base": 890.0, "vol": 0.0006}
}

def generate_market():
    os.makedirs(DB_PATH, exist_ok=True)
    temp_csv_dir = "/tmp/tick_db_market_gen"
    os.makedirs(temp_csv_dir, exist_ok=True)

    random.seed(42)
    # Initialize prices
    active_prices = {sym: meta["base"] for sym, meta in SYMBOLS.items()}

    print(f"Generating realistic market data partitions across {len(DATES)} trading days in {DB_PATH} ...")

    for day_idx, date_str in enumerate(DATES):
        csv_path = os.path.join(temp_csv_dir, f"{date_str}.csv")
        rows = []
        start_dt = datetime.datetime.strptime(f"{date_str} 09:15:00", "%Y-%m-%d %H:%M:%S")

        # Small realistic overnight gap at market open (+/- 0.15%)
        if day_idx > 0:
            for sym, meta in SYMBOLS.items():
                overnight_gap = random.gauss(0.0, 0.0015)
                active_prices[sym] = round(active_prices[sym] * (1.0 + overnight_gap), 2)

        for m in range(376):
            cur_time = start_dt + datetime.timedelta(minutes=m)
            time_str = cur_time.strftime("%Y-%m-%d %H:%M:%S")

            for sym, meta in SYMBOLS.items():
                prev_close = active_prices[sym]
                # Slight mean reversion toward base price to keep candles realistic and stable
                reversion = (meta["base"] - prev_close) * 0.0005
                pct_change = reversion + random.gauss(0.0, meta["vol"])
                
                new_close = round(prev_close * (1.0 + pct_change), 2)
                spread = abs(random.gauss(0.0, meta["base"] * 0.0004))
                high = round(max(prev_close, new_close) + spread, 2)
                low = round(min(prev_close, new_close) - spread, 2)
                open_px = prev_close
                volume = int(random.randint(1500, 18000))

                active_prices[sym] = new_close
                rows.append((time_str, sym, open_px, high, low, new_close, volume))

        # Sort strictly by timestamp ascending to maintain temporal integrity
        rows.sort(key=lambda x: (x[0], x[1]))

        with open(csv_path, "w") as f:
            f.write("timestamp,symbol,open,high,low,close,volume\n")
            for r in rows:
                f.write(f"{r[0]},{r[1]},{r[2]:.2f},{r[3]:.2f},{r[4]:.2f},{r[5]:.2f},{r[6]}\n")

        cmd = [
            INGEST_BIN,
            "--csv", csv_path,
            "--schema", "ohlcv",
            "--date", date_str,
            "--db-path", DB_PATH,
            "--symbol", "MULTI"
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"Error ingesting {date_str}: {res.stderr}")
        else:
            print(f"[{day_idx+1}/{len(DATES)}] Ingested {date_str} (376 bars/sym) -> Close INFY: ${active_prices['INFY']:.2f}")

    print("\n[SUCCESS] Successfully generated continuous multi-week market data!")

if __name__ == "__main__":
    generate_market()
