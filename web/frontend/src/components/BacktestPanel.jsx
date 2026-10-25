import React, { useState } from 'react';
import { Play, TrendingUp, TrendingDown, Percent, Award, ShieldAlert, Download, Layers } from 'lucide-react';
import EquityChart from './EquityChart';

export default function BacktestPanel({
  symbol,
  startTime,
  endTime,
  strategies = [],
  onRunBacktest,
  isLoading,
  backtestResults
}) {
  const [selectedStrategyId, setSelectedStrategyId] = useState('sma_crossover');
  const [parameters, setParameters] = useState({
    fast_period: 9,
    slow_period: 21,
    period: 14,
    oversold: 30,
    overbought: 70,
    lookback: 20
  });
  const [initialCapital, setInitialCapital] = useState(100000);
  const [commissionRate, setCommissionRate] = useState(0.0005);
  const [viewMode, setViewMode] = useState('equity'); // 'equity' or 'trades'

  const currentStrategy = strategies.find(s => s.id === selectedStrategyId) || strategies[0];

  const handleParamChange = (key, value) => {
    setParameters(prev => ({
      ...prev,
      [key]: Number(value)
    }));
  };

  const handleExecute = () => {
    // Extract relevant params for chosen strategy
    let filteredParams = {};
    if (selectedStrategyId === 'sma_crossover') {
      filteredParams = {
        fast_period: parameters.fast_period,
        slow_period: parameters.slow_period
      };
    } else if (selectedStrategyId === 'rsi') {
      filteredParams = {
        period: parameters.period,
        oversold: parameters.oversold,
        overbought: parameters.overbought
      };
    } else if (selectedStrategyId === 'breakout') {
      filteredParams = {
        lookback: parameters.lookback
      };
    }

    onRunBacktest({
      symbol,
      start_time: startTime,
      end_time: endTime,
      strategy_id: selectedStrategyId,
      parameters: filteredParams,
      initial_capital: initialCapital,
      commission_rate: commissionRate
    });
  };

  const exportCSV = () => {
    if (!backtestResults || !backtestResults.trades || !backtestResults.trades.length) return;
    const headers = ['Symbol,Side,Entry Time,Exit Time,Shares,Entry Price,Exit Price,Gross PnL,Net PnL,Return %\n'];
    const rows = backtestResults.trades.map(t =>
      `${t.symbol},${t.side},${t.entry_time},${t.exit_time},${t.shares},${t.entry_price},${t.exit_price},${t.gross_pnl},${t.net_pnl},${t.return_pct}`
    );
    const blob = new Blob([headers.concat(rows.join('\n'))], { type: 'text/csv' });
    const url = window.URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `backtest_${symbol}_${selectedStrategyId}.csv`;
    a.click();
    window.URL.revokeObjectURL(url);
  };

  const isProfitable = (backtestResults?.net_pnl ?? 0) >= 0;

  return (
    <div className="space-y-6">
      {/* Strategy Configuration Card */}
      <div className="rounded-xl border border-slate-800 bg-slate-900/90 p-5 shadow-xl">
        <div className="flex flex-col md:flex-row md:items-center justify-between gap-4 mb-4 border-b border-slate-800 pb-4">
          <div className="flex items-center gap-3">
            <div className="w-8 h-8 rounded-lg bg-blue-500/10 border border-blue-500/20 flex items-center justify-center text-blue-400">
              <Layers className="w-4 h-4" />
            </div>
            <div>
              <h3 className="text-sm font-semibold text-slate-100">Strategy Parameters</h3>
              <p className="text-xs text-slate-400">Run event-driven stream simulations on tick-db raw tick data</p>
            </div>
          </div>

          {/* Strategy Dropdown */}
          <div className="flex items-center gap-2">
            <label className="text-xs text-slate-400 font-medium">Strategy:</label>
            <select
              value={selectedStrategyId}
              onChange={(e) => setSelectedStrategyId(e.target.value)}
              className="bg-slate-950 border border-slate-700 rounded-lg px-3 py-1.5 text-xs text-slate-200 focus:outline-none focus:border-blue-500 font-mono"
            >
              {strategies.map((strat) => (
                <option key={strat.id} value={strat.id}>
                  {strat.name}
                </option>
              ))}
            </select>
          </div>
        </div>

        {/* Dynamic Parameter Fields */}
        <div className="grid grid-cols-2 md:grid-cols-4 gap-4 mb-5 text-xs">
          {selectedStrategyId === 'sma_crossover' && (
            <>
              <div>
                <label className="block text-slate-400 mb-1">Fast Period (bars)</label>
                <input
                  type="number"
                  min="2"
                  max="100"
                  value={parameters.fast_period}
                  onChange={(e) => handleParamChange('fast_period', e.target.value)}
                  className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-slate-200 font-mono focus:border-blue-500 focus:outline-none"
                />
              </div>
              <div>
                <label className="block text-slate-400 mb-1">Slow Period (bars)</label>
                <input
                  type="number"
                  min="5"
                  max="300"
                  value={parameters.slow_period}
                  onChange={(e) => handleParamChange('slow_period', e.target.value)}
                  className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-slate-200 font-mono focus:border-blue-500 focus:outline-none"
                />
              </div>
            </>
          )}

          {selectedStrategyId === 'rsi' && (
            <>
              <div>
                <label className="block text-slate-400 mb-1">RSI Period</label>
                <input
                  type="number"
                  min="2"
                  max="50"
                  value={parameters.period}
                  onChange={(e) => handleParamChange('period', e.target.value)}
                  className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-slate-200 font-mono focus:border-blue-500 focus:outline-none"
                />
              </div>
              <div>
                <label className="block text-slate-400 mb-1">Oversold Level</label>
                <input
                  type="number"
                  min="10"
                  max="45"
                  value={parameters.oversold}
                  onChange={(e) => handleParamChange('oversold', e.target.value)}
                  className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-slate-200 font-mono focus:border-blue-500 focus:outline-none"
                />
              </div>
              <div>
                <label className="block text-slate-400 mb-1">Overbought Level</label>
                <input
                  type="number"
                  min="55"
                  max="90"
                  value={parameters.overbought}
                  onChange={(e) => handleParamChange('overbought', e.target.value)}
                  className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-slate-200 font-mono focus:border-blue-500 focus:outline-none"
                />
              </div>
            </>
          )}

          {selectedStrategyId === 'breakout' && (
            <div>
              <label className="block text-slate-400 mb-1">Lookback (bars)</label>
              <input
                type="number"
                min="5"
                max="200"
                value={parameters.lookback}
                onChange={(e) => handleParamChange('lookback', e.target.value)}
                className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-slate-200 font-mono focus:border-blue-500 focus:outline-none"
              />
            </div>
          )}

          <div>
            <label className="block text-slate-400 mb-1">Initial Capital ($)</label>
            <input
              type="number"
              step="10000"
              value={initialCapital}
              onChange={(e) => setInitialCapital(Number(e.target.value))}
              className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-slate-200 font-mono focus:border-blue-500 focus:outline-none"
            />
          </div>

          <div>
            <label className="block text-slate-400 mb-1">Commission Rate (%)</label>
            <input
              type="number"
              step="0.0001"
              value={commissionRate * 100}
              onChange={(e) => setCommissionRate(Number(e.target.value) / 100)}
              className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-slate-200 font-mono focus:border-blue-500 focus:outline-none"
            />
          </div>
        </div>

        {/* Execution Trigger Button */}
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 border-t border-slate-800 pt-4">
          <div className="text-xs text-slate-400">
            Targeting <span className="font-semibold text-slate-200 font-mono">{symbol}</span> from{' '}
            <span className="font-mono text-blue-400 font-medium">{startTime}</span> to{' '}
            <span className="font-mono text-blue-400 font-medium">{endTime}</span>
          </div>
          <button
            onClick={handleExecute}
            disabled={isLoading}
            className="flex items-center justify-center gap-2 px-6 py-2.5 rounded-lg bg-blue-600 hover:bg-blue-500 text-white text-xs font-semibold uppercase tracking-wider transition shadow-lg shadow-blue-600/30 disabled:opacity-50"
          >
            {isLoading ? (
              <>
                <div className="w-4 h-4 border-2 border-white border-t-transparent rounded-full animate-spin" />
                <span>Simulating...</span>
              </>
            ) : (
              <>
                <Play className="w-4 h-4 fill-current" />
                <span>Run Quantitative Backtest</span>
              </>
            )}
          </button>
        </div>
      </div>

      {/* Backtest Results Dashboard */}
      {backtestResults && (
        <div className="space-y-6">
          {/* Key Metrics Cards */}
          <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-6 gap-3">
            {/* Total Return */}
            <div className="rounded-xl border border-slate-800 bg-slate-900/80 p-3.5">
              <span className="text-[11px] text-slate-400 flex items-center justify-between">
                <span>Net Return</span>
                {isProfitable ? (
                  <TrendingUp className="w-3.5 h-3.5 text-emerald-400" />
                ) : (
                  <TrendingDown className="w-3.5 h-3.5 text-rose-400" />
                )}
              </span>
              <div className={`text-lg font-bold font-mono mt-1 ${isProfitable ? 'text-emerald-400' : 'text-rose-400'}`}>
                {backtestResults.return_pct >= 0 ? `+${backtestResults.return_pct}%` : `${backtestResults.return_pct}%`}
              </div>
              <span className="text-[10px] text-slate-500 font-mono">
                ${backtestResults.net_pnl?.toLocaleString()} PnL
              </span>
            </div>

            {/* Win Rate */}
            <div className="rounded-xl border border-slate-800 bg-slate-900/80 p-3.5">
              <span className="text-[11px] text-slate-400 flex items-center justify-between">
                <span>Win Rate</span>
                <Award className="w-3.5 h-3.5 text-blue-400" />
              </span>
              <div className="text-lg font-bold font-mono text-slate-100 mt-1">
                {backtestResults.win_rate_pct}%
              </div>
              <span className="text-[10px] text-slate-500 font-mono">
                {backtestResults.winning_trades}W / {backtestResults.losing_trades}L ({backtestResults.total_trades} trades)
              </span>
            </div>

            {/* Profit Factor */}
            <div className="rounded-xl border border-slate-800 bg-slate-900/80 p-3.5">
              <span className="text-[11px] text-slate-400 flex items-center justify-between">
                <span>Profit Factor</span>
                <Percent className="w-3.5 h-3.5 text-amber-400" />
              </span>
              <div className="text-lg font-bold font-mono text-slate-100 mt-1">
                {backtestResults.profit_factor}
              </div>
              <span className="text-[10px] text-slate-500 font-mono">Gross Gain / Loss</span>
            </div>

            {/* Max Drawdown */}
            <div className="rounded-xl border border-slate-800 bg-slate-900/80 p-3.5">
              <span className="text-[11px] text-slate-400 flex items-center justify-between">
                <span>Max Drawdown</span>
                <ShieldAlert className="w-3.5 h-3.5 text-rose-400" />
              </span>
              <div className="text-lg font-bold font-mono text-rose-400 mt-1">
                -{backtestResults.max_drawdown_pct}%
              </div>
              <span className="text-[10px] text-slate-500 font-mono">
                -${backtestResults.max_drawdown?.toLocaleString()} peak drop
              </span>
            </div>

            {/* Sharpe Ratio */}
            <div className="rounded-xl border border-slate-800 bg-slate-900/80 p-3.5">
              <span className="text-[11px] text-slate-400 flex items-center justify-between">
                <span>Sharpe Ratio</span>
                <TrendingUp className="w-3.5 h-3.5 text-indigo-400" />
              </span>
              <div className="text-lg font-bold font-mono text-slate-100 mt-1">
                {backtestResults.sharpe_ratio}
              </div>
              <span className="text-[10px] text-slate-500 font-mono">Annualized Risk-Adj</span>
            </div>

            {/* Total Bars Processed */}
            <div className="rounded-xl border border-slate-800 bg-slate-900/80 p-3.5">
              <span className="text-[11px] text-slate-400">Bars Processed</span>
              <div className="text-lg font-bold font-mono text-slate-100 mt-1">
                {backtestResults.total_bars?.toLocaleString() || 0}
              </div>
              <span className="text-[10px] text-emerald-400 font-mono">Fast C++ Stream</span>
            </div>
          </div>

          {/* Equity Chart & Trade History Tabs */}
          <div className="space-y-4">
            <div className="flex items-center justify-between border-b border-slate-800 pb-3">
              <div className="flex items-center gap-2">
                <button
                  onClick={() => setViewMode('equity')}
                  className={`px-3 py-1.5 rounded-lg text-xs font-semibold transition ${
                    viewMode === 'equity'
                      ? 'bg-slate-800 text-white'
                      : 'text-slate-400 hover:text-slate-200'
                  }`}
                >
                  Portfolio Equity Curve
                </button>
                <button
                  onClick={() => setViewMode('trades')}
                  className={`px-3 py-1.5 rounded-lg text-xs font-semibold transition ${
                    viewMode === 'trades'
                      ? 'bg-slate-800 text-white'
                      : 'text-slate-400 hover:text-slate-200'
                  }`}
                >
                  Trade Log ({backtestResults.trades?.length || 0})
                </button>
              </div>

              {viewMode === 'trades' && backtestResults.trades?.length > 0 && (
                <button
                  onClick={exportCSV}
                  className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs font-mono transition"
                >
                  <Download className="w-3.5 h-3.5" />
                  Export CSV
                </button>
              )}
            </div>

            {viewMode === 'equity' ? (
              <EquityChart
                equityCurve={backtestResults.equity_curve || []}
                initialCapital={backtestResults.initial_capital}
                height={260}
              />
            ) : (
              <div className="rounded-xl border border-slate-800 bg-slate-900 overflow-hidden shadow-xl">
                <div className="overflow-x-auto max-h-80">
                  <table className="w-full text-left text-xs font-mono">
                    <thead className="bg-slate-950 text-slate-400 sticky top-0 border-b border-slate-800 uppercase tracking-wider text-[10px]">
                      <tr>
                        <th className="py-2.5 px-4">Side</th>
                        <th className="py-2.5 px-4">Entry Time</th>
                        <th className="py-2.5 px-4">Exit Time</th>
                        <th className="py-2.5 px-4">Entry Px</th>
                        <th className="py-2.5 px-4">Exit Px</th>
                        <th className="py-2.5 px-4">Shares</th>
                        <th className="py-2.5 px-4">Return %</th>
                        <th className="py-2.5 px-4 text-right">Net PnL</th>
                      </tr>
                    </thead>
                    <tbody className="divide-y divide-slate-800/60">
                      {backtestResults.trades?.length === 0 ? (
                        <tr>
                          <td colSpan="8" className="text-center py-6 text-slate-500">
                            No trades executed for this parameter set.
                          </td>
                        </tr>
                      ) : (
                        backtestResults.trades?.map((t, idx) => {
                          const isWin = t.net_pnl >= 0;
                          return (
                            <tr key={idx} className="hover:bg-slate-800/40 transition">
                              <td className="py-2 px-4">
                                <span
                                  className={`px-2 py-0.5 rounded text-[10px] font-bold ${
                                    t.side === 'BUY'
                                      ? 'bg-emerald-500/10 text-emerald-400 border border-emerald-500/20'
                                      : 'bg-rose-500/10 text-rose-400 border border-rose-500/20'
                                  }`}
                                >
                                  {t.side}
                                </span>
                              </td>
                              <td className="py-2 px-4 text-slate-300">{t.entry_time}</td>
                              <td className="py-2 px-4 text-slate-400">{t.exit_time}</td>
                              <td className="py-2 px-4 text-slate-200">${t.entry_price.toFixed(2)}</td>
                              <td className="py-2 px-4 text-slate-200">${t.exit_price.toFixed(2)}</td>
                              <td className="py-2 px-4 text-slate-400">{t.shares}</td>
                              <td className={`py-2 px-4 font-semibold ${isWin ? 'text-emerald-400' : 'text-rose-400'}`}>
                                {t.return_pct >= 0 ? `+${t.return_pct.toFixed(2)}%` : `${t.return_pct.toFixed(2)}%`}
                              </td>
                              <td className={`py-2 px-4 text-right font-bold ${isWin ? 'text-emerald-400' : 'text-rose-400'}`}>
                                {t.net_pnl >= 0 ? `+$${t.net_pnl.toFixed(2)}` : `-$${Math.abs(t.net_pnl).toFixed(2)}`}
                              </td>
                            </tr>
                          );
                        })
                      )}
                    </tbody>
                  </table>
                </div>
              </div>
            )}
          </div>
        </div>
      )}
    </div>
  );
}
