import os
import asyncio
from typing import Optional, List, Dict, Any
from fastapi import FastAPI, Query, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from db_client import TickDBClient
from strategies import AVAILABLE_STRATEGIES, get_strategy
from backtester import BacktestEngine

app = FastAPI(
    title="tick-db Web Analyzer & Backtester API",
    description="Sub-microsecond Parquet + R-Tree Spatial Query & Real-Time Backtesting Engine",
    version="1.0.0"
)

# Enable CORS for local web frontend (Vite/React, Next.js, etc.)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

client = TickDBClient()

class BacktestRequest(BaseModel):
    symbol: str
    start_time: str
    end_time: str
    strategy_id: str
    parameters: Dict[str, Any] = {}
    initial_capital: float = 100000.0
    commission_rate: float = 0.0005

@app.get("/api/health")
def health_check():
    return {
        "status": "online",
        "engine": "tick-db high-performance query engine",
        "cli_path": client.cli_path,
        "cli_exists": os.path.exists(client.cli_path)
    }

@app.get("/api/metadata")
def get_metadata():
    return client.get_metadata()

@app.get("/api/symbols")
def get_symbols():
    return {"symbols": client.get_symbols()}

@app.get("/api/strategies")
def list_strategies():
    return {
        "strategies": [
            {
                "id": "sma_crossover",
                "name": "SMA Crossover",
                "description": "Trend following strategy with fast and slow moving averages.",
                "default_params": {"fast_period": 9, "slow_period": 21}
            },
            {
                "id": "rsi",
                "name": "RSI Mean Reversion",
                "description": "Momentum oscillator identifying overbought and oversold conditions.",
                "default_params": {"period": 14, "oversold": 30.0, "overbought": 70.0}
            },
            {
                "id": "breakout",
                "name": "Donchian Breakout",
                "description": "Buys on new N-bar highs and sells on new N-bar lows.",
                "default_params": {"lookback": 20}
            }
        ]
    }

@app.get("/api/candles")
async def get_candles(
    symbol: str = Query(..., description="Ticker symbol (e.g., INFY, LTTS, RELIANCE)"),
    start_time: str = Query("20230102:09:15:00", description="Start timestamp YYYYMMDD:hh:mm:ss"),
    end_time: str = Query("20230102:15:30:00", description="End timestamp YYYYMMDD:hh:mm:ss"),
    price_filter: Optional[str] = Query(None, description="Optional price filter, e.g. price > 1500"),
    timeframe: str = Query("auto", description="Timeframe aggregation: 1m, 5m, 15m, 1h, 1d, auto")
):
    try:
        candles = await client.query_ohlcv(symbol, start_time, end_time, price_filter, timeframe)
        return {
            "symbol": symbol,
            "timeframe": timeframe,
            "count": len(candles),
            "candles": candles
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/backtest")
async def run_backtest(req: BacktestRequest):
    if req.strategy_id not in AVAILABLE_STRATEGIES:
        raise HTTPException(
            status_code=400,
            detail=f"Strategy '{req.strategy_id}' not found. Available: {list(AVAILABLE_STRATEGIES.keys())}"
        )

    strategy = get_strategy(req.strategy_id, **req.parameters)
    engine = BacktestEngine(
        strategy=strategy,
        initial_capital=req.initial_capital,
        commission_rate=req.commission_rate
    )

    bar_count = 0
    async for bar in client.stream_ohlcv_json([req.symbol], req.start_time, req.end_time):
        engine.process_bar(bar)
        bar_count += 1

    if bar_count == 0:
        raise HTTPException(
            status_code=404,
            detail=f"No market data found for {req.symbol} between {req.start_time} and {req.end_time}"
        )

    results = engine.calculate_results()
    results["symbol"] = req.symbol
    results["total_bars"] = bar_count
    return results

@app.websocket("/ws/stream")
async def websocket_stream(websocket: WebSocket):
    """
    WebSocket endpoint for real-time bar-by-bar streaming replay.
    Client can send:
    { "symbol": "INFY", "start_time": "20230102:09:15:00", "end_time": "20230102:15:30:00", "delay_ms": 50 }
    """
    await websocket.accept()
    try:
        init_data = await websocket.receive_json()
        symbol = init_data.get("symbol", "INFY")
        start_time = init_data.get("start_time", "20230102:09:15:00")
        end_time = init_data.get("end_time", "20230102:15:30:00")
        delay_ms = float(init_data.get("delay_ms", 50)) / 1000.0

        async for bar in client.stream_ohlcv_json([symbol], start_time, end_time):
            await websocket.send_json(bar)
            if delay_ms > 0:
                await asyncio.sleep(delay_ms)

        await websocket.send_json({"type": "STREAM_END", "message": "Replay finished"})
    except WebSocketDisconnect:
        pass
    except Exception as e:
        await websocket.send_json({"type": "ERROR", "message": str(e)})

# Mount built frontend SPA static files if available
FRONTEND_DIST = os.path.abspath(os.path.join(os.path.dirname(__file__), "../frontend/dist"))
if os.path.exists(FRONTEND_DIST):
    app.mount("/", StaticFiles(directory=FRONTEND_DIST, html=True), name="frontend")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=True)
