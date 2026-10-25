import React from 'react';
import { Play, Pause, RotateCcw, Zap, FastForward } from 'lucide-react';

export default function ReplayControls({
  isPlaying,
  isFinished,
  onPlay,
  onPause,
  onReset,
  onReplayDaySession,
  speedMs,
  setSpeedMs,
  barCount,
  currentBar,
  totalBarsEstimate = 0
}) {
  const isLargeRange = totalBarsEstimate > 3000;

  return (
    <div className="space-y-2">
      <div className="rounded-xl border border-slate-800 bg-slate-900/90 p-4 shadow-xl flex flex-wrap items-center justify-between gap-4">
        {/* Play/Pause/Reset Controls */}
        <div className="flex items-center gap-2">
          {!isPlaying ? (
            <button
              onClick={onPlay}
              disabled={isFinished}
              className={`flex items-center gap-2 px-4 py-2 rounded-lg text-xs font-semibold uppercase tracking-wider transition shadow-lg ${
                isFinished
                  ? 'bg-slate-800 text-slate-500 cursor-not-allowed'
                  : 'bg-emerald-600 hover:bg-emerald-500 text-white shadow-emerald-600/30'
              }`}
            >
              <Play className="w-4 h-4 fill-current" />
              {isFinished ? 'Finished' : 'Start Replay'}
            </button>
          ) : (
            <button
              onClick={onPause}
              className="flex items-center gap-2 px-4 py-2 rounded-lg bg-amber-600 hover:bg-amber-500 text-white text-xs font-semibold uppercase tracking-wider transition shadow-lg shadow-amber-600/30"
            >
              <Pause className="w-4 h-4 fill-current" />
              Pause
            </button>
          )}

          <button
            onClick={onReset}
            className="flex items-center gap-1.5 px-3 py-2 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs font-medium transition"
          >
            <RotateCcw className="w-3.5 h-3.5" />
            Reset
          </button>

          {onReplayDaySession && (
            <button
              onClick={onReplayDaySession}
              title="Switch to 1-Day (375 bars) for fast realistic replay"
              className="flex items-center gap-1 px-3 py-2 rounded-lg bg-blue-600/20 border border-blue-500/30 hover:bg-blue-600/30 text-blue-400 text-xs font-medium transition"
            >
              <FastForward className="w-3.5 h-3.5" />
              Replay 1-Day (375 Bars)
            </button>
          )}
        </div>

        {/* Speed Slider */}
        <div className="flex items-center gap-3">
          <div className="flex items-center gap-1.5 text-xs text-slate-400">
            <Zap className="w-3.5 h-3.5 text-amber-400" />
            <span>Delay:</span>
            <span className="font-mono font-semibold text-slate-200">{speedMs}ms</span>
          </div>
          <input
            type="range"
            min="5"
            max="200"
            step="5"
            value={speedMs}
            onChange={(e) => setSpeedMs(Number(e.target.value))}
            className="w-24 accent-blue-500 cursor-pointer"
          />
          <div className="flex gap-1">
            {[100, 50, 20, 5].map((ms) => (
              <button
                key={ms}
                onClick={() => setSpeedMs(ms)}
                className={`px-2 py-0.5 text-[10px] rounded font-mono ${
                  speedMs === ms ? 'bg-blue-600 text-white' : 'bg-slate-800 text-slate-400 hover:text-slate-200'
                }`}
              >
                {ms === 5 ? 'MAX' : `${ms}ms`}
              </button>
            ))}
          </div>
        </div>

        {/* Status & Latest Bar Info */}
        <div className="flex items-center gap-4 text-xs font-mono">
          <div className="flex items-center gap-2">
            <span className="text-slate-400">Streamed:</span>
            <span className="font-bold text-blue-400">{barCount.toLocaleString()}</span>
          </div>
          {currentBar && (
            <div className="flex items-center gap-3 bg-slate-950 px-3 py-1.5 rounded-lg border border-slate-800">
              <span className="text-slate-400">{currentBar.time}</span>
              <span className="text-slate-500">|</span>
              <span className="font-bold text-emerald-400">${(currentBar.close ?? currentBar.price)?.toFixed(2)}</span>
              <span className="text-slate-500">|</span>
              <span className="text-blue-400">Vol: {currentBar.volume?.toLocaleString()}</span>
            </div>
          )}
        </div>
      </div>

      {/* Large Range Helper Notice */}
      {isLargeRange && (
        <div className="px-4 py-2 rounded-lg bg-amber-500/10 border border-amber-500/20 text-amber-300 text-xs flex items-center justify-between">
          <span>
            ℹ️ Selected range spans a large timeframe (~{totalBarsEstimate.toLocaleString()} bars). Replay simulates live tick-by-tick market action one bar at a time.
          </span>
          <button
            onClick={onReplayDaySession}
            className="ml-3 underline hover:text-white font-medium whitespace-nowrap"
          >
            Click to Replay 1 Day (375 bars)
          </button>
        </div>
      )}
    </div>
  );
}
