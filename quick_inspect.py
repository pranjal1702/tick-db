import glob
import os

files = glob.glob('/home/pranjalsingh/tickDB/**/data.parquet', recursive=True)
print('Total files:', len(files))
date_dirs = set()
for f in files:
    parts = f.split(os.sep)
    for p in parts:
        if p.startswith('date='):
            date_dirs.add(p)

print('Unique dates in DB:', sorted(list(date_dirs))[:10])
print('Latest 10 dates:', sorted(list(date_dirs))[-10:])
