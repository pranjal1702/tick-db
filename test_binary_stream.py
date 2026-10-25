import subprocess
import struct

# Run tick_db_cli in --binary-stream mode
cmd = [
    "./build/tick_db_cli",
    "--db-path", "/home/pranjalsingh/tickDB",
    "--binary-stream",
    "--sql", "SELECT * FROM ohlcv WHERE ts_exchange_ns BETWEEN 20230102:09:15:00 AND 20230102:09:20:00 AND symbol = 'LTTS';"
]

proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

# Binary format: uint64(ts), 8 char(symbol), int64(open), int64(high), int64(low), int64(close), int64(vol) = 56 bytes per packet
PACKET_SIZE = 56
packet_format = "<Q8sqqqqq"

count = 0
while True:
    raw = proc.stdout.read(PACKET_SIZE)
    if not raw or len(raw) < PACKET_SIZE:
        break
    ts, sym_bytes, open_p, high_p, low_p, close_p, vol = struct.unpack(packet_format, raw)
    sym = sym_bytes.decode('ascii').rstrip('\x00')
    count += 1
    if count <= 5:
        print(f"Packet #{count} -> Time: {ts} ns, Symbol: {sym}, Close: ${close_p / 1e8:.2f}, Volume: {vol}")

proc.wait()
print(f"Successfully unpacked {count} binary streaming packets!")
