import React, { useState, useEffect, useRef } from 'react';
import Header from './components/Header';
import TradingViewChart from './components/TradingViewChart';
import ReplayControls from './components/ReplayControls';
import BacktestPanel from './components/BacktestPanel';
import {
  fetchHealth,
  fetchMetadata,
  fetchSymbols,
  fetchStrategies,
  fetchCandles,
  runBacktest,
  createReplayWebSocket
} from './services/api';
import { AlertCircle } from 'lucide-react';

export default function App() {
  const [activeTab, setActiveTab] = useState('analyzer');
  const [health, setHealth] = useState(null);
  const [metadata, setMetadata] = useState(null);
  const [symbols, setSymbols] = useState([]);
  const [strategies, setStrategies] = useState([]);

  // Active Query Parameters
  const [selectedSymbol, setSelectedSymbol] = useState('INFY');
  const [startTime, setStartTime] = useState('20230102:09:15:00');
  const [endTime, setEndTime] = useState('20230103:15:30:00');
  const [timeframe, setTimeframe] = useState('auto');
  const [priceFilter, setPriceFilter] = useState('');

  // Editable Input Fields (to avoid querying on every keystroke)
  const [inputStartTime, setInputStartTime] = useState('20230102:09:15:00');
  const [inputEndTime, setInputEndTime] = useState('20230103:15:30:00');

  // Data & State
  const [candles, setCandles] = useState([]);
  const [isLoadingCandles, setIsLoadingCandles] = useState(false);
  const [errorMsg, setErrorMsg] = useState(null);
  const abortRef = useRef(null);

  // Replay Streaming State
  const [isReplaying, setIsReplaying] = useState(false);
  const [isReplayFinished, setIsReplayFinished] = useState(false);
  const [speedMs, setSpeedMs] = useState(20);
  const [streamingBar, setStreamingBar] = useState(null);
  const [replayBarCount, setReplayBarCount] = useState(0);
  const replayCounterRef = useRef(0);
  const wsRef = useRef(null);

  // Backtest State
  const [isBacktesting, setIsBacktesting] = useState(false);
  const [backtestResults, setBacktestResults] = useState(null);
  const [tradeMarkers, setTradeMarkers] = useState([]);

  // Load Initial Metadata & Health
  const loadInitialData = async () => {
    try {
      const [h, meta, syms, strats] = await Promise.all([
        fetchHealth().catch(() => ({ status: 'offline' })),
        fetchMetadata().catch(() => null),
        fetchSymbols().catch(() => ['INFY', 'LTTS']),
        fetchStrategies().catch(() => [])
      ]);
      setHealth(h);
      setMetadata(meta);
      setSymbols(syms);
      setStrategies(strats);
      if (syms.length && !syms.includes(selectedSymbol)) {
        setSelectedSymbol(syms[0]);
      }
    } catch (err) {
      console.error('Init error:', err);
    }
  };

  useEffect(() => {
    loadInitialData();
  }, []);

  // Fetch Candles for Selected Symbol & Range with AbortController
  const loadCandles = async () => {
    if (!selectedSymbol || isReplaying) return;

    if (abortRef.current) {
      abortRef.current.abort();
    }
    abortRef.current = new AbortController();

    setIsLoadingCandles(true);
    setErrorMsg(null);
    try {
      const res = await fetchCandles(
        selectedSymbol,
        startTime,
        endTime,
        priceFilter || null,
        timeframe,
        abortRef.current.signal
      );
      setCandles(res.candles || []);
    } catch (err) {
      if (err.name === 'AbortError') return;
      setErrorMsg(err.message);
    } finally {
      setIsLoadingCandles(false);
    }
  };

  useEffect(() => {
    loadCandles();
  }, [selectedSymbol, startTime, endTime, timeframe]);

  // Handle Manual Apply
  const handleApplyRange = () => {
    setStartTime(inputStartTime);
    setEndTime(inputEndTime);
  };

  // Handle Preset Ranges
  const setDatePreset = (preset) => {
    let s = '20230102:09:15:00';
    let e = '20230102:15:30:00';
    if (preset === 'day1') {
      s = '20230102:09:15:00';
      e = '20230102:15:30:00';
    } else if (preset === 'day2') {
      s = '20230103:09:15:00';
      e = '20230103:15:30:00';
    } else if (preset === 'multiday') {
      s = '20230102:09:15:00';
      e = '20230106:15:30:00';
    } else if (preset === 'month' || preset === '2years') {
      s = '20230102:09:15:00';
      e = '20230127:15:30:00';
    }
    setInputStartTime(s);
    setInputEndTime(e);
    setStartTime(s);
    setEndTime(e);
  };

  // Replay Streaming Handlers
  const startReplayWithRange = (sTime, eTime) => {
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }

    setCandles([]); // Clear initial static candles so new bars stream in cleanly
    setStreamingBar(null);
    replayCounterRef.current = 0;
    setReplayBarCount(0);
    setIsReplaying(true);
    setIsReplayFinished(false);

    wsRef.current = createReplayWebSocket(
      {
        symbol: selectedSymbol,
        startTime: sTime,
        endTime: eTime,
        delayMs: speedMs
      },
      (bar) => {
        replayCounterRef.current += 1;
        setStreamingBar(bar);
        // Throttle React state counter updates so UI stays at 60fps
        if (replayCounterRef.current % 5 === 0 || replayCounterRef.current === 1) {
          setReplayBarCount(replayCounterRef.current);
        }
      },
      () => {
        setIsReplaying(false);
        setIsReplayFinished(true);
        setReplayBarCount(replayCounterRef.current);
      },
      (err) => {
        console.error('WebSocket Replay Error:', err);
        setIsReplaying(false);
      }
    );
  };

  const handleStartReplay = () => {
    startReplayWithRange(startTime, endTime);
  };

  const handleReplayDaySession = () => {
    const s = '20230102:09:15:00';
    const e = '20230102:15:30:00';
    setInputStartTime(s);
    setInputEndTime(e);
    setStartTime(s);
    setEndTime(e);
    startReplayWithRange(s, e);
  };

  const handlePauseReplay = () => {
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }
    setIsReplaying(false);
  };

  const handleResetReplay = () => {
    handlePauseReplay();
    setIsReplayFinished(false);
    setReplayBarCount(0);
    setStreamingBar(null);
    loadCandles();
  };

  // Run Backtest
  const handleRunBacktest = async (config) => {
    setIsBacktesting(true);
    setErrorMsg(null);
    try {
      const res = await runBacktest(config);
      setBacktestResults(res);

      // Create Markers for Chart (Buy and Sell entry arrows)
      if (res.trades && res.trades.length) {
        const markers = [];
        res.trades.forEach(t => {
          markers.push({
            time: t.entry_time,
            side: t.side,
            price: t.entry_price
          });
        });
        setTradeMarkers(markers);
      } else {
        setTradeMarkers([]);
      }
    } catch (err) {
      setErrorMsg(err.message);
    } finally {
      setIsBacktesting(false);
    }
  };

  // Synchronize inputs when active startTime / endTime changes externally
  useEffect(() => {
    setInputStartTime(startTime);
    setInputEndTime(endTime);
  }, [startTime, endTime]);

  const activeTf = candles[0]?.timeframe || timeframe;

  return (
    <div className="min-h-screen bg-[#0b0f19] text-slate-100 flex flex-col font-sans">
      <Header
        health={health}
        metadata={metadata}
        activeTab={activeTab}
        setActiveTab={setActiveTab}
        onRefresh={loadInitialData}
      />

      <main className="flex-1 max-w-7xl w-full mx-auto p-4 md:p-6 space-y-6">
        {/* Error Alert */}
        {errorMsg && (
          <div className="flex items-center gap-3 px-4 py-3 rounded-xl bg-rose-500/10 border border-rose-500/20 text-rose-400 text-xs">
            <AlertCircle className="w-4 h-4 flex-shrink-0" />
            <span>{errorMsg}</span>
          </div>
        )}

        {/* Global Control Bar */}
        <div className="rounded-xl border border-slate-800 bg-slate-900/90 p-4 shadow-xl flex flex-wrap items-center justify-between gap-4">
          {/* Symbol Selector */}
          <div className="flex items-center gap-2">
            <label className="text-xs text-slate-400 font-medium">Symbol:</label>
            <div className="relative">
              <select
                value={selectedSymbol}
                onChange={(e) => setSelectedSymbol(e.target.value)}
                className="bg-slate-950 border border-slate-700 rounded-lg px-3 py-1.5 text-xs font-mono font-semibold text-slate-100 focus:outline-none focus:border-blue-500 cursor-pointer"
              >
                {symbols.map(s => (
                  <option key={s} value={s}>
                    {s}
                  </option>
                ))}
              </select>
            </div>
          </div>

          {/* Timeframe Selector */}
          <div className="flex items-center gap-2">
            <label className="text-xs text-slate-400 font-medium">Timeframe:</label>
            <select
              value={timeframe}
              onChange={(e) => setTimeframe(e.target.value)}
              className="bg-slate-950 border border-slate-700 rounded-lg px-2.5 py-1.5 font-mono text-slate-200 text-xs focus:border-blue-500 focus:outline-none cursor-pointer"
            >
              <option value="auto">Auto (Smart)</option>
              <option value="1m">1-Min</option>
              <option value="5m">5-Min</option>
              <option value="15m">15-Min</option>
              <option value="1h">1-Hour</option>
              <option value="1d">1-Day</option>
            </select>
          </div>

          {/* Date Range Inputs with Apply Button */}
          <div className="flex flex-wrap items-center gap-2 text-xs">
            <div className="flex items-center gap-1.5">
              <span className="text-slate-400">Start:</span>
              <input
                type="text"
                value={inputStartTime}
                onChange={(e) => setInputStartTime(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && handleApplyRange()}
                className="bg-slate-950 border border-slate-800 rounded-lg px-2.5 py-1.5 font-mono text-slate-200 text-xs focus:border-blue-500 focus:outline-none"
                placeholder="YYYYMMDD:hh:mm:ss"
              />
            </div>
            <div className="flex items-center gap-1.5">
              <span className="text-slate-400">End:</span>
              <input
                type="text"
                value={inputEndTime}
                onChange={(e) => setInputEndTime(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && handleApplyRange()}
                className="bg-slate-950 border border-slate-800 rounded-lg px-2.5 py-1.5 font-mono text-slate-200 text-xs focus:border-blue-500 focus:outline-none"
                placeholder="YYYYMMDD:hh:mm:ss"
              />
            </div>
            <button
              onClick={handleApplyRange}
              disabled={isLoadingCandles}
              className="px-3 py-1.5 rounded-lg bg-blue-600 hover:bg-blue-500 text-white font-semibold text-xs tracking-wider transition disabled:opacity-50 shadow"
            >
              Apply
            </button>
          </div>

          {/* Quick Date Presets */}
          <div className="flex items-center gap-1.5">
            <button
              onClick={() => setDatePreset('day1')}
              className="px-2.5 py-1 rounded bg-slate-800 hover:bg-slate-700 text-[11px] text-slate-300 transition"
            >
              Jan 02 (1D)
            </button>
            <button
              onClick={() => setDatePreset('day2')}
              className="px-2.5 py-1 rounded bg-slate-800 hover:bg-slate-700 text-[11px] text-slate-300 transition"
            >
              Jan 03 (1D)
            </button>
            <button
              onClick={() => setDatePreset('multiday')}
              className="px-2.5 py-1 rounded bg-blue-600/20 border border-blue-500/30 text-[11px] text-blue-400 font-medium hover:bg-blue-600/30 transition"
            >
              Jan 02-06 (1W)
            </button>
            <button
              onClick={() => setDatePreset('month')}
              className="px-2.5 py-1 rounded bg-indigo-600/20 border border-indigo-500/30 text-[11px] text-indigo-400 font-medium hover:bg-indigo-600/30 transition"
            >
              Jan 02-27 (4W)
            </button>
          </div>
        </div>

        {/* View Mode: Analyzer vs Backtest */}
        {activeTab === 'analyzer' ? (
          <div className="space-y-4">
            {/* Replay Controller Bar */}
            <ReplayControls
              isPlaying={isReplaying}
              isFinished={isReplayFinished}
              onPlay={handleStartReplay}
              onPause={handlePauseReplay}
              onReset={handleResetReplay}
              onReplayDaySession={handleReplayDaySession}
              speedMs={speedMs}
              setSpeedMs={setSpeedMs}
              barCount={isReplaying ? replayBarCount : candles.length}
              currentBar={streamingBar || candles[candles.length - 1]}
              totalBarsEstimate={candles.length}
            />

            {/* TradingView Candlestick Chart */}
            <TradingViewChart
              candles={candles}
              markers={tradeMarkers}
              streamingBar={streamingBar}
              symbol={selectedSymbol}
              isLoading={isLoadingCandles}
              isReplaying={isReplaying}
              timeframe={activeTf}
              height={560}
            />
          </div>
        ) : (
          <div className="space-y-6">
            {/* TradingView Candlestick Chart with Backtest Trade Overlays */}
            <TradingViewChart
              candles={candles}
              markers={tradeMarkers}
              symbol={selectedSymbol}
              isLoading={isLoadingCandles}
              timeframe={activeTf}
              height={420}
            />

            {/* Backtest Strategy Engine Panel */}
            <BacktestPanel
              symbol={selectedSymbol}
              startTime={startTime}
              endTime={endTime}
              strategies={strategies}
              onRunBacktest={handleRunBacktest}
              isLoading={isBacktesting}
              backtestResults={backtestResults}
            />
          </div>
        )}
      </main>

      {/* Footer */}
      <footer className="border-t border-slate-800/80 py-4 px-6 text-center text-xs text-slate-500 font-mono">
        tick-db Sub-Microsecond Multi-Dimensional Spatial Engine & Real-Time Backtesting Framework
      </footer>
    </div>
  );
}
