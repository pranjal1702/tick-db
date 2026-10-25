from typing import Dict, Any
from .base import BaseStrategy, Signal

class RsiStrategy(BaseStrategy):
    def __init__(self, period: int = 14, oversold: float = 30.0, overbought: float = 70.0):
        super().__init__("RSI Mean Reversion", {
            "period": period,
            "oversold": oversold,
            "overbought": overbought
        })
        self.period = int(period)
        self.oversold = float(oversold)
        self.overbought = float(overbought)
        self.prices = []
        self.gains = []
        self.losses = []
        self.prev_rsi = None

    def on_bar(self, bar: Dict[str, Any]) -> str:
        price = bar["close"]
        self.prices.append(price)

        if len(self.prices) < 2:
            return Signal.HOLD

        change = self.prices[-1] - self.prices[-2]
        self.gains.append(max(0.0, change))
        self.losses.append(max(0.0, -change))

        if len(self.gains) < self.period:
            return Signal.HOLD

        avg_gain = sum(self.gains[-self.period:]) / self.period
        avg_loss = sum(self.losses[-self.period:]) / self.period

        if avg_loss == 0:
            rsi = 100.0
        else:
            rs = avg_gain / avg_loss
            rsi = 100.0 - (100.0 / (1.0 + rs))

        signal = Signal.HOLD
        if self.prev_rsi is not None:
            if self.prev_rsi <= self.oversold and rsi > self.oversold:
                signal = Signal.BUY
            elif self.prev_rsi >= self.overbought and rsi < self.overbought:
                signal = Signal.SELL

        self.prev_rsi = rsi
        return signal

    def reset(self):
        super().reset()
        self.prices = []
        self.gains = []
        self.losses = []
        self.prev_rsi = None
