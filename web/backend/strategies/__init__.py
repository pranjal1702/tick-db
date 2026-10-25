from .base import BaseStrategy, Signal
from .sma_crossover import SmaCrossoverStrategy
from .rsi_strategy import RsiStrategy
from .breakout import ChannelBreakoutStrategy

AVAILABLE_STRATEGIES = {
    "sma_crossover": SmaCrossoverStrategy,
    "rsi": RsiStrategy,
    "breakout": ChannelBreakoutStrategy
}

def get_strategy(strategy_id: str, **params) -> BaseStrategy:
    cls = AVAILABLE_STRATEGIES.get(strategy_id)
    if not cls:
        raise ValueError(f"Unknown strategy: {strategy_id}. Available: {list(AVAILABLE_STRATEGIES.keys())}")
    return cls(**params)
