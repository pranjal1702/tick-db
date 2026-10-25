from typing import Dict, Any
from .base import BaseStrategy, Signal

class SmaCrossoverStrategy(BaseStrategy):
    def __init__(self, fast_period: int = 9, slow_period: int = 21):
        super().__init__("SMA Crossover", {"fast_period": fast_period, "slow_period": slow_period})
        self.fast_period = int(fast_period)
        self.slow_period = int(slow_period)
        self.prices = []
        self.prev_fast = None
        self.prev_slow = None

    def on_bar(self, bar: Dict[str, Any]) -> str:
        price = bar["close"]
        self.prices.append(price)

        if len(self.prices) < self.slow_period:
            return Signal.HOLD

        fast_sma = sum(self.prices[-self.fast_period:]) / self.fast_period
        slow_sma = sum(self.prices[-self.slow_period:]) / self.slow_period

        signal = Signal.HOLD
        if self.prev_fast is not None and self.prev_slow is not None:
            # Bullish crossover
            if self.prev_fast <= self.prev_slow and fast_sma > slow_sma:
                signal = Signal.BUY
            # Bearish crossover
            elif self.prev_fast >= self.prev_slow and fast_sma < slow_sma:
                signal = Signal.SELL

        self.prev_fast = fast_sma
        self.prev_slow = slow_sma
        return signal

    def reset(self):
        super().reset()
        self.prices = []
        self.prev_fast = None
        self.prev_slow = None
