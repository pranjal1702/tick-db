./build/tick_db_cli --db-path /home/pranjalsingh/tickDB --sql "SELECT * FROM ohlcv WHERE ts_exchange_ns BETWEEN 20230102:15:25:00 AND 20230103:14:20:00 AND symbol = 'LTTS' AND price > 3750;"
