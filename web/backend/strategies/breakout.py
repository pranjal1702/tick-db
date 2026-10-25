from typing import Dict, Any
from .base import BaseStrategy, Signal

class ChannelBreakoutStrategy(BaseStrategy):
    def __init__(self, lookback: int = 20):
        super().__init__("Donchian Breakout", {"lookback": lookback})
        self.lookback = int(lookback)
        self.highs = []
        self.lows = []

    def on_bar(self, bar: Dict[str, Any]) -> str:
        high = bar["high"]
        low = bar["low"]
        close = bar["close"]

        if len(self.highs) < self.lookback:
            self.highs.append(high)
            self.lows.append(low)
            return Signal.HOLD

        upper_channel = max(self.highs[-self.lookback:])
        lower_channel = min(self.lows[-self.lookback:])

        signal = Signal.HOLD
        if close > upper_channel:
            signal = Signal.BUY
        elif close < lower_channel:
            signal = Signal.SELL

        self.highs.append(high)
        self.lows.append(low)
        return signal

    def reset(self):
        super().reset()
        self.highs = []
        self.lows = []
