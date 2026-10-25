from abc import ABC, abstractmethod
from typing import Dict, Any, Optional

class Signal:
    HOLD = "HOLD"
    BUY = "BUY"
    SELL = "SELL"
    CLOSE = "CLOSE"

class BaseStrategy(ABC):
    def __init__(self, name: str, params: Optional[Dict[str, Any]] = None):
        self.name = name
        self.params = params or {}
        self.bars = []

    @abstractmethod
    def on_bar(self, bar: Dict[str, Any]) -> str:
        """
        Process a new OHLCV bar and return a Signal:
        - Signal.BUY
        - Signal.SELL
        - Signal.CLOSE
        - Signal.HOLD
        """
        pass

    def reset(self):
        self.bars = []
