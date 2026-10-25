import React, { useEffect, useRef } from 'react';
import { createChart, ColorType } from 'lightweight-charts';
import { parseTickDBTimestamp } from '../services/api';

export default function EquityChart({ equityCurve = [], initialCapital = 100000, height = 240 }) {
  const containerRef = useRef(null);
  const chartRef = useRef(null);
  const areaSeriesRef = useRef(null);
  const benchmarkRef = useRef(null);

  useEffect(() => {
    if (!containerRef.current) return;

    const chart = createChart(containerRef.current, {
      width: containerRef.current.clientWidth,
      height: height,
      layout: {
        background: { type: ColorType.Solid, color: '#0f172a' },
        textColor: '#94a3b8',
        fontSize: 11,
        fontFamily: "'JetBrains Mono', monospace"
      },
      grid: {
        vertLines: { color: 'rgba(30, 41, 59, 0.4)' },
        horzLines: { color: 'rgba(30, 41, 59, 0.4)' }
      },
      rightPriceScale: {
        borderColor: '#1e293b',
        scaleMargins: {
          top: 0.15,
          bottom: 0.15
        }
      },
      timeScale: {
        borderColor: '#1e293b',
        timeVisible: true,
        secondsVisible: false
      }
    });

    const areaSeries = chart.addAreaSeries({
      topColor: 'rgba(59, 130, 246, 0.4)',
      bottomColor: 'rgba(59, 130, 246, 0.0)',
      lineColor: '#3b82f6',
      lineWidth: 2
    });

    chartRef.current = chart;
    areaSeriesRef.current = areaSeries;

    const handleResize = () => {
      if (containerRef.current && chart) {
        chart.applyOptions({
          width: containerRef.current.clientWidth,
          height: height
        });
      }
    };

    window.addEventListener('resize', handleResize);

    return () => {
      window.removeEventListener('resize', handleResize);
      chart.remove();
    };
  }, [height]);

  useEffect(() => {
    if (!areaSeriesRef.current || !equityCurve.length) return;

    const formatted = [];
    const seenTimes = new Set();

    const sorted = [...equityCurve].sort((a, b) => {
      const tA = parseTickDBTimestamp(a.time);
      const tB = parseTickDBTimestamp(b.time);
      return tA - tB;
    });

    for (const pt of sorted) {
      const timeSec = parseTickDBTimestamp(pt.time);
      if (seenTimes.has(timeSec)) continue;
      seenTimes.add(timeSec);
      formatted.push({
        time: timeSec,
        value: Number(pt.equity)
      });
    }

    if (formatted.length > 0) {
      areaSeriesRef.current.setData(formatted);
      if (chartRef.current) {
        chartRef.current.timeScale().fitContent();
      }
    }
  }, [equityCurve]);

  return (
    <div className="w-full rounded-xl overflow-hidden border border-slate-800 bg-slate-900 shadow-xl p-3">
      <div className="flex items-center justify-between mb-2 px-1">
        <div className="flex items-center gap-2">
          <div className="w-2.5 h-2.5 rounded-full bg-blue-500 shadow-[0_0_8px_rgba(59,130,246,0.6)]" />
          <h4 className="text-xs font-semibold uppercase tracking-wider text-slate-300">Portfolio Equity Curve</h4>
        </div>
        <span className="text-[11px] font-mono text-slate-400">
          Initial: ${Number(initialCapital).toLocaleString()}
        </span>
      </div>
      <div ref={containerRef} className="w-full" style={{ height: `${height}px` }} />
    </div>
  );
}
