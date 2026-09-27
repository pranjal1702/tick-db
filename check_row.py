import pyarrow.parquet as pq

pf = pq.ParquetFile('/home/pranjalsingh/tickDB/date=2023-01-02/ohlcv/data.parquet')
df = pf.read().to_pandas()
print('Total rows on 2023-01-02:', len(df))
print('Symbols present:', df['symbol'].unique()[:10])
print('First row raw values:')
row0 = df.iloc[0]
for col in df.columns:
    print(f'  {col}: {row0[col]} (type: {type(row0[col])})')
