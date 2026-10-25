#!/bin/bash
# Startup script for tick-db Web Analyzer & Backtester

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKEND_DIR="$PROJECT_ROOT/web/backend"
FRONTEND_DIR="$PROJECT_ROOT/web/frontend"

echo "=========================================================="
echo " Starting tick-db Web Analyzer & Backtester"
echo "=========================================================="

# Check if tick_db_cli binary exists
if [ ! -f "$PROJECT_ROOT/build/tick_db_cli" ]; then
    echo "[!] tick_db_cli not found in build/. Building now..."
    cd "$PROJECT_ROOT" && mkdir -p build && cd build && cmake .. && make -j4 tick_db_cli
fi

# Check if frontend is built
if [ ! -d "$FRONTEND_DIR/dist" ]; then
    echo "[*] Building frontend assets..."
    cd "$FRONTEND_DIR" && npm run build
fi

echo "[*] Starting FastAPI server on http://localhost:8000"
echo "    - Web Dashboard:  http://localhost:8000"
echo "    - API Docs:       http://localhost:8000/docs"
echo "    - WebSocket:      ws://localhost:8000/ws/stream"
echo "=========================================================="

cd "$PROJECT_ROOT"
python3 -m uvicorn main:app --app-dir "$BACKEND_DIR" --host 0.0.0.0 --port 8000 --reload
