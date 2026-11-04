import React, { useEffect, useRef } from 'react';
import { createChart, ColorType, CrosshairMode } from 'lightweight-charts';
import { parseTickDBTimestamp } from '../services/api';

export default function TradingViewChart({
  candles = [],
  markers = [],
  streamingBar = null,
  height = 520,
  symbol = 'TICK',
  isLoading = false,
  isReplaying = false,
  timeframe = '1m'
}) {
  const chartContainerRef = useRef(null);
  const chartRef = useRef(null);
  const candleSeriesRef = useRef(null);
  const volumeSeriesRef = useRef(null);
  const tooltipRef = useRef(null);

  // Initialize TradingView Chart
  useEffect(() => {
    if (!chartContainerRef.current) return;

    const chart = createChart(chartContainerRef.current, {
      width: chartContainerRef.current.clientWidth,
      height: height,
      layout: {
        background: { type: ColorType.Solid, color: '#0f172a' },
        textColor: '#94a3b8',
        fontSize: 12,
        fontFamily: "'JetBrains Mono', monospace"
      },
      grid: {
        vertLines: { color: 'rgba(30, 41, 59, 0.6)' },
        horzLines: { color: 'rgba(30, 41, 59, 0.6)' }
      },
      crosshair: {
        mode: CrosshairMode.Normal,
        vertLine: {
          color: '#64748b',
          width: 1,
          style: 2,
          labelBackgroundColor: '#1e293b'
        },
        horzLine: {
          color: '#64748b',
          width: 1,
          style: 2,
          labelBackgroundColor: '#1e293b'
        }
      },
      rightPriceScale: {
        borderColor: '#1e293b',
        scaleMargins: {
          top: 0.1,
          bottom: 0.25
        }
      },
      localization: {
        dateFormat: 'yyyy-MM-dd',
      },
      timeScale: {
        borderColor: '#1e293b',
        timeVisible: true,
        secondsVisible: false,
        tickMarkFormatter: (time, tickMarkType) => {
          const d = new Date(time * 1000);
          const yyyy = d.getUTCFullYear();
          const monthNames = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
          const mon = monthNames[d.getUTCMonth()];
          const day = String(d.getUTCDate()).padStart(2, '0');
          const hh = String(d.getUTCHours()).padStart(2, '0');
          const mm = String(d.getUTCMinutes()).padStart(2, '0');

          // 0: Year, 1: Month, 2: DayOfMonth, 3: Time, 4: TimeWithSeconds
          if (tickMarkType === 0) return `${yyyy}`;
          if (tickMarkType === 1) return `${mon} ${yyyy}`;
          if (tickMarkType === 2) return `${day} ${mon}`;
          return `${hh}:${mm}`;
        }
      }
    });

    // Main Candlestick Series
    const candleSeries = chart.addCandlestickSeries({
      upColor: '#10b981',
      downColor: '#ef4444',
      borderUpColor: '#10b981',
      borderDownColor: '#ef4444',
      wickUpColor: '#10b981',
      wickDownColor: '#ef4444'
    });

    candleSeries.priceScale().applyOptions({
      scaleMargins: {
        top: 0.1,    // 10% margin at top
        bottom: 0.25 // 25% margin at bottom so candles never overlap volume
      }
    });

    // Volume Histogram (overlayed cleanly at bottom)
    const volumeSeries = chart.addHistogramSeries({
      priceFormat: { type: 'volume' },
      priceScaleId: '', // Separate overlay scale
    });

    volumeSeries.priceScale().applyOptions({
      scaleMargins: {
        top: 0.8, // Push volume bars down so they only occupy bottom 20%
        bottom: 0
      }
    });

    chartRef.current = chart;
    candleSeriesRef.current = candleSeries;
    volumeSeriesRef.current = volumeSeries;

    // Crosshair tooltip hover handler
    chart.subscribeCrosshairMove((param) => {
      if (!tooltipRef.current) return;
      if (
        !param.point ||
        !param.time ||
        param.point.x < 0 ||
        param.point.x > chartContainerRef.current.clientWidth ||
        param.point.y < 0 ||
        param.point.y > height
      ) {
        tooltipRef.current.style.display = 'none';
        return;
      }

      const candleData = param.seriesData.get(candleSeries);
      const volumeData = param.seriesData.get(volumeSeries);

      if (candleData) {
        const d = new Date(param.time * 1000);
        const yyyy = d.getUTCFullYear();
        const mm = String(d.getUTCMonth() + 1).padStart(2, '0');
        const dd = String(d.getUTCDate()).padStart(2, '0');
        const hh = String(d.getUTCHours()).padStart(2, '0');
        const min = String(d.getUTCMinutes()).padStart(2, '0');
        const dateFormatted = `${yyyy}-${mm}-${dd} ${hh}:${min}`;

        const isUp = candleData.close >= candleData.open;
        const colorClass = isUp ? 'text-emerald-400' : 'text-rose-400';
        tooltipRef.current.style.display = 'block';
        tooltipRef.current.innerHTML = `
          <div class="flex items-center gap-3">
            <span class="font-bold text-slate-100">${symbol}</span>
            <span class="text-blue-400 font-mono font-medium">${dateFormatted}</span>
            <span>O: <span class="${colorClass}">${candleData.open.toFixed(2)}</span></span>
            <span>H: <span class="${colorClass}">${candleData.high.toFixed(2)}</span></span>
            <span>L: <span class="${colorClass}">${candleData.low.toFixed(2)}</span></span>
            <span>C: <span class="${colorClass}">${candleData.close.toFixed(2)}</span></span>
            ${volumeData ? `<span>Vol: <span class="text-slate-300 font-mono">${volumeData.value.toLocaleString()}</span></span>` : ''}
          </div>
        `;
      }
    });

    // Responsive window resize observer
    const handleResize = () => {
      if (chartContainerRef.current && chart) {
        chart.applyOptions({
          width: chartContainerRef.current.clientWidth,
          height: height
        });
      }
    };

    window.addEventListener('resize', handleResize);

    return () => {
      window.removeEventListener('resize', handleResize);
      chart.remove();
    };
  }, [height, symbol]);

  // Update Candlestick and Volume Data when candles change
  useEffect(() => {
    if (!candleSeriesRef.current || !volumeSeriesRef.current) return;
    if (!candles.length) {
      candleSeriesRef.current.setData([]);
      volumeSeriesRef.current.setData([]);
      return;
    }

    // Sort and deduplicate timestamps
    const formattedCandles = [];
    const formattedVolume = [];
    const seenTimes = new Set();

    // Copy and sort by timestamp
    const sorted = [...candles].sort((a, b) => {
      const tA = parseTickDBTimestamp(a.time || a.timestamp_ns);
      const tB = parseTickDBTimestamp(b.time || b.timestamp_ns);
      return tA - tB;
    });

    for (const c of sorted) {
      const timeSec = parseTickDBTimestamp(c.time || c.timestamp_ns);
      if (seenTimes.has(timeSec)) continue;
      seenTimes.add(timeSec);

      const open = c.open ?? c.price;
      const high = c.high ?? Math.max(open, c.close ?? c.price);
      const low = c.low ?? Math.min(open, c.close ?? c.price);
      const close = c.close ?? c.price;
      const vol = c.volume ?? 0;

      formattedCandles.push({
        time: timeSec,
        open: Number(open),
        high: Number(high),
        low: Number(low),
        close: Number(close)
      });

      formattedVolume.push({
        time: timeSec,
        value: Number(vol),
        color: close >= open ? 'rgba(16, 185, 129, 0.4)' : 'rgba(239, 68, 68, 0.4)'
      });
    }

    candleSeriesRef.current.setData(formattedCandles);
    volumeSeriesRef.current.setData(formattedVolume);

    if (chartRef.current && formattedCandles.length > 0) {
      chartRef.current.timeScale().fitContent();
    }
  }, [candles]);

  // Update Markers (Buy / Sell entry & exits)
  useEffect(() => {
    if (!candleSeriesRef.current) return;
    if (markers && markers.length > 0) {
      const formattedMarkers = markers
        .map(m => {
          const timeSec = parseTickDBTimestamp(m.time);
          return {
            time: timeSec,
            position: m.side === 'BUY' ? 'belowBar' : 'aboveBar',
            color: m.side === 'BUY' ? '#10b981' : '#ef4444',
            shape: m.side === 'BUY' ? 'arrowUp' : 'arrowDown',
            text: `${m.side} @ ${m.price.toFixed(1)}`
          };
        })
        .sort((a, b) => a.time - b.time);

      candleSeriesRef.current.setMarkers(formattedMarkers);
    } else {
      candleSeriesRef.current.setMarkers([]);
    }
  }, [markers]);

  // Real-time streaming bar updates
  useEffect(() => {
    if (!streamingBar || !candleSeriesRef.current || !volumeSeriesRef.current) return;
    const timeSec = parseTickDBTimestamp(streamingBar.time || streamingBar.timestamp_ns);
    const open = streamingBar.open ?? streamingBar.price;
    const close = streamingBar.close ?? streamingBar.price;
    const high = streamingBar.high ?? Math.max(open, close);
    const low = streamingBar.low ?? Math.min(open, close);
    const vol = streamingBar.volume ?? 0;

    candleSeriesRef.current.update({
      time: timeSec,
      open: Number(open),
      high: Number(high),
      low: Number(low),
      close: Number(close)
    });

    volumeSeriesRef.current.update({
      time: timeSec,
      value: Number(vol),
      color: close >= open ? 'rgba(16, 185, 129, 0.4)' : 'rgba(239, 68, 68, 0.4)'
    });
  }, [streamingBar]);

  return (
    <div className="relative w-full rounded-xl overflow-hidden border border-slate-800 bg-slate-900 shadow-2xl">
      {/* Legend & Hover Tooltip Banner */}
      <div
        ref={tooltipRef}
        className="absolute top-3 left-3 z-20 px-3 py-1.5 rounded-md bg-slate-900/90 border border-slate-800 text-xs font-num text-slate-300 pointer-events-none shadow"
        style={{ display: 'none' }}
      />

      {/* Status Badges Top Right */}
      <div className="absolute top-3 right-3 z-10 flex items-center gap-2">
        {isReplaying && (
          <span className="flex items-center gap-1.5 px-2.5 py-1 rounded bg-rose-500/10 border border-rose-500/30 text-[11px] font-mono font-semibold text-rose-400 animate-pulse">
            <span className="w-2 h-2 rounded-full bg-rose-500 shadow-[0_0_8px_rgba(244,63,94,0.9)]" />
            REPLAY ACTIVE
          </span>
        )}
        <span className="px-2.5 py-1 rounded bg-slate-800/80 border border-slate-700/80 text-xs font-semibold text-slate-200 uppercase tracking-wider">
          {symbol}
        </span>
        <span className="px-2 py-0.5 rounded bg-blue-500/10 border border-blue-500/20 text-[10px] font-mono text-blue-400 uppercase">
          {timeframe || '1-MIN'}
        </span>
      </div>

      {/* Loading Overlay */}
      {isLoading && (
        <div className="absolute inset-0 z-30 flex items-center justify-center bg-slate-950/60 backdrop-blur-sm">
          <div className="flex flex-col items-center gap-2">
            <div className="w-8 h-8 border-2 border-blue-500 border-t-transparent rounded-full animate-spin" />
            <span className="text-xs text-slate-400 font-mono">Executing tick-db R-Tree query...</span>
          </div>
        </div>
      )}

      {/* Empty State Overlay */}
      {!isLoading && !isReplaying && (!candles || candles.length === 0) && (
        <div className="absolute inset-0 z-20 flex items-center justify-center bg-slate-950/40">
          <div className="text-center p-6 rounded-xl bg-slate-900/80 border border-slate-800 text-xs text-slate-400 font-mono">
            No market data found for {symbol} in the selected time window.
          </div>
        </div>
      )}

      {/* Main Chart Container */}
      <div ref={chartContainerRef} className="w-full" style={{ height: `${height}px` }} />
    </div>
  );
}
