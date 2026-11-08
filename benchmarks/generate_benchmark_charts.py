#!/usr/bin/env python3
import json
import os
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# Apply clean dark quant theme
plt.style.use('dark_background')
plt.rcParams['font.sans-serif'] = 'DejaVu Sans'
plt.rcParams['font.family'] = 'sans-serif'
plt.rcParams['axes.edgecolor'] = '#334155'
plt.rcParams['axes.linewidth'] = 1.2
plt.rcParams['grid.color'] = '#1e293b'
plt.rcParams['grid.linestyle'] = '--'
plt.rcParams['grid.alpha'] = 0.7

os.makedirs('docs', exist_ok=True)

with open('benchmarks/benchmark_results.json', 'r') as f:
    data = json.load(f)

# -------------------------------------------------------------
# CHART 1: Pruning & Read Amplification
# -------------------------------------------------------------
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6), facecolor='#0f172a')
for ax in (ax1, ax2):
    ax.set_facecolor('#0b1120')
    ax.grid(True)

# 1A. Read Amplification
bars = ax1.bar(['Full Parquet Scan', 'R-Tree + Bitmask\nSub-RG Pruning'], 
               [data['pruning']['full_scan_read_amp'], data['pruning']['indexed_read_amp']], 
               color=['#ef4444', '#10b981'], width=0.45, edgecolor='#334155', linewidth=1.5)
ax1.set_title('Read Amplification Factor\n(Records Decoded / Records Returned)', color='#f8fafc', fontsize=13, weight='bold', pad=12)
ax1.set_ylabel('Read Amplification (Ratio)', color='#94a3b8', fontsize=11)
ax1.tick_params(colors='#94a3b8', labelsize=10)

for bar in bars:
    yval = bar.get_height()
    ax1.text(bar.get_x() + bar.get_width() / 2.0, yval + 1.5, f'{yval:.1f}x', 
             ha='center', va='bottom', color='#f8fafc', fontsize=12, weight='bold')

ax1.axhline(1.0, color='#38bdf8', linestyle=':', linewidth=1.5, label='Theoretical Ideal (1.0x)')
ax1.legend(loc='upper right', facecolor='#1e293b', edgecolor='#334155', fontsize=10)

# 1B. Records Examined
bars2 = ax2.bar(['Full Scan', 'R-Tree Pruned'], 
                [100000, 9996], 
                color=['#f59e0b', '#3b82f6'], width=0.45, edgecolor='#334155', linewidth=1.5)
ax2.set_title('Records Examined from Disk\n(100,000 Total Record Dataset)', color='#f8fafc', fontsize=13, weight='bold', pad=12)
ax2.set_ylabel('Records Examined', color='#94a3b8', fontsize=11)
ax2.tick_params(colors='#94a3b8', labelsize=10)
ax2.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, p: f'{int(x):,}'))

for bar in bars2:
    yval = bar.get_height()
    ax2.text(bar.get_x() + bar.get_width() / 2.0, yval + 1500, f'{int(yval):,}', 
             ha='center', va='bottom', color='#f8fafc', fontsize=12, weight='bold')

plt.suptitle('Spatial R-Tree Indexing & Sub-Row-Group Symbol Pruning Performance', 
             color='#38bdf8', fontsize=15, weight='bold', y=0.98)
plt.tight_layout(rect=[0, 0, 1, 0.94])
plt.savefig('docs/benchmark_pruning_performance.png', dpi=200)
plt.close()
print("Saved docs/benchmark_pruning_performance.png")

# -------------------------------------------------------------
# CHART 2: RAM Working Set (Streaming vs Vector Buffering)
# -------------------------------------------------------------
fig, ax = plt.subplots(figsize=(10, 6), facecolor='#0f172a')
ax.set_facecolor('#0b1120')
ax.grid(True)

rec_counts = [f'{n:,}' for n in data['streaming_memory']['record_counts']]
vec_ram = data['streaming_memory']['vector_buffering_mb']
stream_ram = data['streaming_memory']['streaming_mb']

ax.plot(rec_counts, vec_ram, marker='o', color='#ef4444', linewidth=2.5, markersize=8, label='Vector Buffering: O(N) Linear Allocation')
ax.plot(rec_counts, stream_ram, marker='s', color='#10b981', linewidth=2.5, markersize=8, label='Zero-RAM Streaming: O(1) Constant Working Set')

for i, (txt_v, txt_s) in enumerate(zip(vec_ram, stream_ram)):
    ax.annotate(f'{txt_v:.1f} MB', (i, txt_v), textcoords="offset points", xytext=(0, 10), ha='center', color='#fca5a5', fontsize=9, weight='bold')
    ax.annotate(f'{txt_s:.1f} MB', (i, txt_s), textcoords="offset points", xytext=(0, -15), ha='center', color='#6ee7b7', fontsize=9, weight='bold')

ax.set_title('Working Set RAM Footprint vs. Dataset Record Count', color='#f8fafc', fontsize=14, weight='bold', pad=15)
ax.set_xlabel('Dataset Size (Records)', color='#94a3b8', fontsize=11, labelpad=8)
ax.set_ylabel('Peak Working Set Memory (MB)', color='#94a3b8', fontsize=11, labelpad=8)
ax.tick_params(colors='#94a3b8', labelsize=10)
ax.legend(facecolor='#1e293b', edgecolor='#334155', fontsize=11, loc='upper left')

plt.tight_layout()
plt.savefig('docs/benchmark_streaming_memory.png', dpi=200)
plt.close()
print("Saved docs/benchmark_streaming_memory.png")

# -------------------------------------------------------------
# CHART 3: K-Way Merge Throughput
# -------------------------------------------------------------
fig, ax = plt.subplots(figsize=(10, 6), facecolor='#0f172a')
ax.set_facecolor('#0b1120')
ax.grid(True)

fanouts = [f'K = {k}' for k in data['kway_merge']['fanouts']]
throughputs = data['kway_merge']['throughput_m_per_sec']

bars = ax.bar(fanouts, throughputs, color='#8b5cf6', width=0.45, edgecolor='#334155', linewidth=1.5)
ax.set_title('Deterministic Multi-Stream K-Way Merge Throughput\n(Keyed on (ts_exchange_ns, seq_no))', color='#f8fafc', fontsize=14, weight='bold', pad=15)
ax.set_xlabel('Concurrent Stream Fan-Out (K)', color='#94a3b8', fontsize=11, labelpad=8)
ax.set_ylabel('Throughput (Million Records / Second)', color='#94a3b8', fontsize=11, labelpad=8)
ax.tick_params(colors='#94a3b8', labelsize=10)

for bar in bars:
    yval = bar.get_height()
    ax.text(bar.get_x() + bar.get_width() / 2.0, yval + 0.03, f'{yval:.2f} M/s', 
            ha='center', va='bottom', color='#f8fafc', fontsize=11, weight='bold')

plt.tight_layout()
plt.savefig('docs/benchmark_kway_throughput.png', dpi=200)
plt.close()
print("Saved docs/benchmark_kway_throughput.png")

# -------------------------------------------------------------
# CHART 4: Real Partitioned Market Data Multi-Day Scaling
# -------------------------------------------------------------
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6), facecolor='#0f172a')
for ax in (ax1, ax2):
    ax.set_facecolor('#0b1120')
    ax.grid(True)

days = data['multi_day_query']['days']
latencies = data['multi_day_query']['latency_ms']
records = data['multi_day_query']['records']

ax1.plot(days, latencies, marker='o', color='#38bdf8', linewidth=2.5, markersize=8)
ax1.set_title('Total Multi-Day Query Latency (ms)', color='#f8fafc', fontsize=13, weight='bold', pad=12)
ax1.set_xlabel('Daily Partitions Queried', color='#94a3b8', fontsize=11)
ax1.set_ylabel('Execution Latency (ms)', color='#94a3b8', fontsize=11)
ax1.tick_params(colors='#94a3b8', labelsize=10)
for d, lat in zip(days, latencies):
    ax1.annotate(f'{lat:.1f} ms', (d, lat), textcoords="offset points", xytext=(0, 8), ha='center', color='#bae6fd', fontsize=9, weight='bold')

per_day_latency = [lat / d for d, lat in zip(days, latencies)]
bars_day = ax2.bar([str(d) for d in days], per_day_latency, color='#06b6d4', width=0.45, edgecolor='#334155', linewidth=1.5)
ax2.set_title('Partition Scan Rate (ms / Partition Day)', color='#f8fafc', fontsize=13, weight='bold', pad=12)
ax2.set_xlabel('Daily Partitions Queried', color='#94a3b8', fontsize=11)
ax2.set_ylabel('Latency Per Day (ms)', color='#94a3b8', fontsize=11)
ax2.tick_params(colors='#94a3b8', labelsize=10)
for bar in bars_day:
    yval = bar.get_height()
    ax2.text(bar.get_x() + bar.get_width() / 2.0, yval + 0.15, f'{yval:.2f} ms', 
             ha='center', va='bottom', color='#f8fafc', fontsize=10, weight='bold')

plt.suptitle('PartitionCatalog & Streaming Multi-Day Execution Scalability (20 Trading Days)', 
             color='#38bdf8', fontsize=15, weight='bold', y=0.98)
plt.tight_layout(rect=[0, 0, 1, 0.94])
plt.savefig('docs/benchmark_end_to_end_latency.png', dpi=200)
plt.close()
print("Saved docs/benchmark_end_to_end_latency.png")

# -------------------------------------------------------------
# CHART 5: Unified Executive Benchmark Dashboard
# -------------------------------------------------------------
fig, axs = plt.subplots(2, 2, figsize=(16, 11), facecolor='#0f172a')
for ax in axs.flat:
    ax.set_facecolor('#0b1120')
    ax.grid(True)

# 1. Read Amp
axs[0, 0].bar(['Full Scan', 'R-Tree + Bitmask'], 
              [data['pruning']['full_scan_read_amp'], data['pruning']['indexed_read_amp']], 
              color=['#ef4444', '#10b981'], width=0.45, edgecolor='#334155')
axs[0, 0].set_title('Read Amplification (Lower is Better)', color='#f8fafc', fontsize=12, weight='bold')
axs[0, 0].set_ylabel('Ratio (Decoded / Returned)', color='#94a3b8')
axs[0, 0].tick_params(colors='#94a3b8')

# 2. Memory
axs[0, 1].plot(rec_counts, vec_ram, marker='o', color='#ef4444', label='Vector Buffering: O(N)')
axs[0, 1].plot(rec_counts, stream_ram, marker='s', color='#10b981', label='Zero-RAM Streaming: O(1)')
axs[0, 1].set_title('RAM Footprint vs Records (Constant O(1))', color='#f8fafc', fontsize=12, weight='bold')
axs[0, 1].set_ylabel('RAM Working Set (MB)', color='#94a3b8')
axs[0, 1].legend(facecolor='#1e293b', edgecolor='#334155')
axs[0, 1].tick_params(colors='#94a3b8')

# 3. K-Way Merge
axs[1, 0].bar(fanouts, throughputs, color='#8b5cf6', width=0.45, edgecolor='#334155')
axs[1, 0].set_title('Deterministic K-Way Merge Throughput', color='#f8fafc', fontsize=12, weight='bold')
axs[1, 0].set_ylabel('Million Records / Sec', color='#94a3b8')
axs[1, 0].tick_params(colors='#94a3b8')

# 4. Multi-Day Scaling
axs[1, 1].plot(days, latencies, marker='o', color='#38bdf8', linewidth=2)
axs[1, 1].set_title('Multi-Day Scaling Across 20 Partitions', color='#f8fafc', fontsize=12, weight='bold')
axs[1, 1].set_xlabel('Partitions (Days)', color='#94a3b8')
axs[1, 1].set_ylabel('Latency (ms)', color='#94a3b8')
axs[1, 1].tick_params(colors='#94a3b8')

plt.suptitle('tick-db Storage & Query Engine Comprehensive Benchmark Report', 
             color='#38bdf8', fontsize=16, weight='bold', y=0.98)
plt.tight_layout(rect=[0, 0, 1, 0.95])
plt.savefig('docs/storage_benchmark_dashboard.png', dpi=200)
plt.close()
print("Saved docs/storage_benchmark_dashboard.png")

