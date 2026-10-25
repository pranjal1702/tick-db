// API and WebSocket Client for tick-db

const API_BASE = '/api';

export function formatTickDBTimestamp(dateStr) {
  // Input: "2023-01-02T09:15" or Date object
  // Output: "20230102:09:15:00"
  if (!dateStr) return '';
  const d = new Date(dateStr);
  if (isNaN(d.getTime())) return dateStr;
  const yyyy = d.getFullYear();
  const mm = String(d.getMonth() + 1).padStart(2, '0');
  const dd = String(d.getDate()).padStart(2, '0');
  const hh = String(d.getHours()).padStart(2, '0');
  const min = String(d.getMinutes()).padStart(2, '0');
  const sec = String(d.getSeconds()).padStart(2, '0');
  return `${yyyy}${mm}${dd}:${hh}:${min}:${sec}`;
}

export function parseTickDBTimestamp(tsStr) {
  // Input: "20230102:09:15:00" -> Returns Unix timestamp in seconds for TradingView Lightweight Charts
  if (!tsStr) return 0;
  if (typeof tsStr === 'number') {
    // If it's already nanoseconds or seconds
    return tsStr > 1e12 ? Math.floor(tsStr / 1e9) : tsStr;
  }
  const parts = tsStr.split(':');
  if (parts.length >= 3) {
    const ymd = parts[0];
    const hh = parts[1];
    const mm = parts[2];
    const ss = parts[3] || '00';
    const year = parseInt(ymd.substring(0, 4), 10);
    const month = parseInt(ymd.substring(4, 6), 10) - 1;
    const day = parseInt(ymd.substring(6, 8), 10);
    const date = new Date(Date.UTC(year, month, day, parseInt(hh, 10), parseInt(mm, 10), parseInt(ss, 10)));
    return Math.floor(date.getTime() / 1000);
  }
  return Math.floor(new Date(tsStr).getTime() / 1000);
}

export async function fetchHealth() {
  const res = await fetch(`${API_BASE}/health`);
  if (!res.ok) throw new Error('Engine health check failed');
  return res.json();
}

export async function fetchMetadata() {
  const res = await fetch(`${API_BASE}/metadata`);
  if (!res.ok) throw new Error('Failed to fetch metadata');
  return res.json();
}

export async function fetchSymbols() {
  const res = await fetch(`${API_BASE}/symbols`);
  if (!res.ok) throw new Error('Failed to fetch symbols');
  const data = await res.json();
  return data.symbols || [];
}

export async function fetchStrategies() {
  const res = await fetch(`${API_BASE}/strategies`);
  if (!res.ok) throw new Error('Failed to fetch strategies');
  const data = await res.json();
  return data.strategies || [];
}

export async function fetchCandles(symbol, startTime, endTime, priceFilter = null, timeframe = 'auto', signal = null) {
  const params = new URLSearchParams({
    symbol,
    start_time: startTime,
    end_time: endTime,
    timeframe: timeframe
  });
  if (priceFilter) {
    params.append('price_filter', priceFilter);
  }
  const res = await fetch(`${API_BASE}/candles?${params.toString()}`, { signal });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: 'Failed to fetch candles' }));
    throw new Error(err.detail || 'Failed to fetch candles');
  }
  return res.json();
}

export async function runBacktest(config) {
  const res = await fetch(`${API_BASE}/backtest`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(config)
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: 'Backtest execution failed' }));
    throw new Error(err.detail || 'Backtest execution failed');
  }
  return res.json();
}

export function createReplayWebSocket({ symbol, startTime, endTime, delayMs }, onBar, onEnd, onError) {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const wsUrl = `${protocol}//${window.location.host}/ws/stream`;
  const ws = new WebSocket(wsUrl);

  ws.onopen = () => {
    ws.send(JSON.stringify({
      symbol,
      start_time: startTime,
      end_time: endTime,
      delay_ms: delayMs
    }));
  };

  ws.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      if (data.type === 'STREAM_END') {
        if (onEnd) onEnd(data);
      } else if (data.type === 'ERROR') {
        if (onError) onError(new Error(data.message));
      } else {
        if (onBar) onBar(data);
      }
    } catch (e) {
      if (onError) onError(e);
    }
  };

  ws.onerror = (err) => {
    if (onError) onError(err);
  };

  return ws;
}
