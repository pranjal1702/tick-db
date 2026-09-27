import glob
import pyarrow.parquet as pq

files = glob.glob('/home/pranjalsingh/tickDB/**/*.parquet', recursive=True)
print('Total files found:', len(files))

dates = []
for f in files:
    try:
        pf = pq.ParquetFile(f)
        df = pf.read().to_pandas()
        if 'ts_exchange_ns' in df.columns and len(df) > 0:
            dates.append((f, df['ts_exchange_ns'].min(), df['ts_exchange_ns'].max()))
    except Exception as e:
        pass

dates.sort(key=lambda x: x[1])
print("Found non-empty files:", len(dates))
if dates:
    print("Earliest timestamp file:", dates[0][0])
    print("Earliest timestamp (ns):", dates[0][1], "--> (max):", dates[0][2])
    print("Latest timestamp file:", dates[-1][0])
    print("Latest timestamp (ns):", dates[-1][1], "--> (max):", dates[-1][2])
    print("\nFirst 5 files metadata:")
    for item in dates[:5]:
        print(item[0], item[1], item[2])
