import React from 'react';
import { Database, Activity, RefreshCw, BarChart2, Cpu } from 'lucide-react';

export default function Header({
  metadata,
  health,
  activeTab,
  setActiveTab,
  onRefresh
}) {
  const isOnline = health?.status === 'online';

  return (
    <header className="border-b border-slate-800 bg-slate-950/80 backdrop-blur sticky top-0 z-50 px-6 py-3">
      <div className="max-w-7xl mx-auto flex flex-col md:flex-row items-center justify-between gap-4">
        {/* Brand & Subtitle */}
        <div className="flex items-center gap-3">
          <div className="w-10 h-10 rounded-xl bg-gradient-to-tr from-blue-600 to-indigo-500 flex items-center justify-center shadow-lg shadow-blue-500/20">
            <Database className="w-5 h-5 text-white" />
          </div>
          <div>
            <div className="flex items-center gap-2">
              <h1 className="font-bold text-lg text-slate-100 tracking-tight font-mono">tick-db</h1>
              <span className="px-2 py-0.5 text-[10px] font-mono rounded-full bg-blue-500/10 text-blue-400 border border-blue-500/20">
                v2.0
              </span>
            </div>
            <p className="text-xs text-slate-400 flex items-center gap-1.5">
              <span>Sub-Microsecond R-Tree Parquet Engine</span>
              <span>•</span>
              <span className="text-emerald-400 font-mono">O(1) Memory Stream</span>
            </p>
          </div>
        </div>

        {/* Navigation Tabs */}
        <div className="flex items-center bg-slate-900/90 p-1 rounded-xl border border-slate-800">
          <button
            onClick={() => setActiveTab('analyzer')}
            className={`flex items-center gap-2 px-4 py-1.5 rounded-lg text-xs font-semibold transition-all ${
              activeTab === 'analyzer'
                ? 'bg-blue-600 text-white shadow-md shadow-blue-600/30'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            <BarChart2 className="w-4 h-4" />
            Market Analyzer & Replay
          </button>
          <button
            onClick={() => setActiveTab('backtest')}
            className={`flex items-center gap-2 px-4 py-1.5 rounded-lg text-xs font-semibold transition-all ${
              activeTab === 'backtest'
                ? 'bg-blue-600 text-white shadow-md shadow-blue-600/30'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            <Cpu className="w-4 h-4" />
            Quantitative Backtester
          </button>
        </div>

        {/* Database Health & Partitions Badge */}
        <div className="flex items-center gap-3">
          <div className="flex items-center gap-2 px-3 py-1.5 rounded-lg bg-slate-900 border border-slate-800 text-xs">
            <span
              className={`w-2 h-2 rounded-full ${
                isOnline ? 'bg-emerald-500 shadow-[0_0_8px_rgba(16,185,129,0.8)]' : 'bg-rose-500'
              }`}
            />
            <span className="text-slate-300 font-mono text-[11px]">
              {isOnline ? 'Engine Online' : 'Engine Offline'}
            </span>
            {metadata?.total_partitions && (
              <>
                <span className="text-slate-600">|</span>
                <span className="text-slate-400 font-mono text-[11px]">
                  {metadata.total_partitions} Days ({metadata.date_range?.start?.slice(0, 4)} - {metadata.date_range?.end?.slice(0, 4)})
                </span>
              </>
            )}
          </div>

          <button
            onClick={onRefresh}
            title="Refresh DB Metadata"
            className="p-2 rounded-lg bg-slate-900 border border-slate-800 text-slate-400 hover:text-slate-100 hover:border-slate-700 transition"
          >
            <RefreshCw className="w-3.5 h-3.5" />
          </button>
        </div>
      </div>
    </header>
  );
}
