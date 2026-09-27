./build/tick_db_cli --db-path /home/pranjalsingh/tickDB --sql "SELECT * FROM ohlcv WHERE timestamp BETWEEN 1672617600000000000 AND 1672790399000000000;"
./build/tick_db_cli --db-path /home/pranjalsingh/tickDB --sql "SELECT * FROM ohlcv WHERE ts_exchange_ns BETWEEN 1672617600000000000 AND 1672790399000000000;"
./build/tick_db_cli --db-path /home/pranjalsingh/tickDB --sql "SELECT * FROM ohlcv;"
