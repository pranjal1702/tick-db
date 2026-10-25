import math
from typing import List, Dict, Any, Optional
from strategies.base import BaseStrategy, Signal

class Position:
    def __init__(self, symbol: str, side: str, entry_price: float, shares: int, entry_time: str, entry_ts: int):
        self.symbol = symbol
        self.side = side  # "LONG" or "SHORT"
        self.entry_price = entry_price
        self.shares = shares
        self.entry_time = entry_time
        self.entry_ts = entry_ts

class TradeRecord:
    def __init__(
        self,
        symbol: str,
        side: str,
        entry_price: float,
        exit_price: float,
        shares: int,
        entry_time: str,
        exit_time: str,
        gross_pnl: float,
        net_pnl: float,
        commission: float,
        return_pct: float
    ):
        self.symbol = symbol
        self.side = side
        self.entry_price = entry_price
        self.exit_price = exit_price
        self.shares = shares
        self.entry_time = entry_time
        self.exit_time = exit_time
        self.gross_pnl = gross_pnl
        self.net_pnl = net_pnl
        self.commission = commission
        self.return_pct = return_pct

    def to_dict(self) -> Dict[str, Any]:
        return {
            "symbol": self.symbol,
            "side": self.side,
            "entry_price": round(self.entry_price, 2),
            "exit_price": round(self.exit_price, 2),
            "shares": self.shares,
            "entry_time": self.entry_time,
            "exit_time": self.exit_time,
            "gross_pnl": round(self.gross_pnl, 2),
            "net_pnl": round(self.net_pnl, 2),
            "commission": round(self.commission, 2),
            "return_pct": round(self.return_pct, 2)
        }

class BacktestEngine:
    def __init__(
        self,
        strategy: BaseStrategy,
        initial_capital: float = 100000.0,
        commission_rate: float = 0.0005,  # 0.05% per trade
        fixed_allocation_pct: float = 0.95
    ):
        self.strategy = strategy
        self.initial_capital = initial_capital
        self.commission_rate = commission_rate
        self.allocation_pct = fixed_allocation_pct

        self.cash = initial_capital
        self.position: Optional[Position] = None
        self.trades: List[TradeRecord] = []
        self.equity_curve: List[Dict[str, Any]] = []

    def reset(self):
        self.cash = self.initial_capital
        self.position = None
        self.trades = []
        self.equity_curve = []
        self.strategy.reset()

    def process_bar(self, bar: Dict[str, Any]):
        symbol = bar.get("symbol", "N/A")
        price = bar["close"]
        time_str = bar.get("time", "")
        ts_ns = bar.get("timestamp_ns", 0)

        signal = self.strategy.on_bar(bar)

        # 1. Handle Exits
        if self.position is not None:
            if (self.position.side == "LONG" and signal in (Signal.SELL, Signal.CLOSE)) or \
               (self.position.side == "SHORT" and signal in (Signal.BUY, Signal.CLOSE)):
                self._close_position(price, time_str, ts_ns)

        # 2. Handle Entries
        if self.position is None:
            if signal == Signal.BUY:
                self._open_position(symbol, "LONG", price, time_str, ts_ns)
            elif signal == Signal.SELL:
                self._open_position(symbol, "SHORT", price, time_str, ts_ns)

        # 3. Track Equity
        unrealized_pnl = 0.0
        if self.position is not None:
            if self.position.side == "LONG":
                unrealized_pnl = (price - self.position.entry_price) * self.position.shares
            else:
                unrealized_pnl = (self.position.entry_price - price) * self.position.shares

        current_equity = self.cash + (self.position.shares * price if self.position and self.position.side == "LONG" else 0.0) + (unrealized_pnl if self.position and self.position.side == "SHORT" else 0.0)

        # Sample equity curve (e.g. record points)
        self.equity_curve.append({
            "time": time_str,
            "timestamp_ns": ts_ns,
            "equity": round(current_equity, 2),
            "cash": round(self.cash, 2),
            "close": price,
            "position": self.position.side if self.position else "FLAT"
        })

    def _open_position(self, symbol: str, side: str, price: float, time_str: str, ts_ns: int):
        if price <= 0:
            return
        available_capital = self.cash * self.allocation_pct
        shares = int(available_capital / price)
        if shares <= 0:
            return

        order_value = shares * price
        comm = order_value * self.commission_rate
        if side == "LONG":
            self.cash -= (order_value + comm)
        else:
            self.cash -= comm

        self.position = Position(symbol, side, price, shares, time_str, ts_ns)

    def _close_position(self, exit_price: float, exit_time: str, exit_ts: int):
        pos = self.position
        if not pos:
            return

        order_value = pos.shares * exit_price
        comm = order_value * self.commission_rate

        if pos.side == "LONG":
            gross_pnl = (exit_price - pos.entry_price) * pos.shares
            self.cash += (order_value - comm)
        else:
            gross_pnl = (pos.entry_price - exit_price) * pos.shares
            self.cash += (gross_pnl - comm)

        net_pnl = gross_pnl - (comm * 2.0)  # entry + exit commission
        cost_basis = pos.shares * pos.entry_price
        return_pct = (net_pnl / cost_basis * 100.0) if cost_basis > 0 else 0.0

        record = TradeRecord(
            symbol=pos.symbol,
            side=pos.side,
            entry_price=pos.entry_price,
            exit_price=exit_price,
            shares=pos.shares,
            entry_time=pos.entry_time,
            exit_time=exit_time,
            gross_pnl=gross_pnl,
            net_pnl=net_pnl,
            commission=comm * 2.0,
            return_pct=return_pct
        )
        self.trades.append(record)
        self.position = None

    def calculate_results(self) -> Dict[str, Any]:
        """Compute performance metrics."""
        final_equity = self.equity_curve[-1]["equity"] if self.equity_curve else self.initial_capital
        total_net_pnl = final_equity - self.initial_capital
        total_return_pct = (total_net_pnl / self.initial_capital) * 100.0

        total_trades = len(self.trades)
        winning_trades = [t for t in self.trades if t.net_pnl > 0]
        losing_trades = [t for t in self.trades if t.net_pnl < 0]

        win_rate = (len(winning_trades) / total_trades * 100.0) if total_trades > 0 else 0.0

        gross_profits = sum(t.gross_pnl for t in winning_trades)
        gross_losses = abs(sum(t.gross_pnl for t in losing_trades))
        profit_factor = round(gross_profits / gross_losses, 2) if gross_losses > 0 else (999.0 if gross_profits > 0 else 0.0)

        # Max Drawdown calculation
        peak = self.initial_capital
        max_dd = 0.0
        max_dd_pct = 0.0
        returns = []

        for i, pt in enumerate(self.equity_curve):
            eq = pt["equity"]
            if eq > peak:
                peak = eq
            dd = peak - eq
            dd_pct = (dd / peak) * 100.0 if peak > 0 else 0.0
            if dd > max_dd:
                max_dd = dd
            if dd_pct > max_dd_pct:
                max_dd_pct = dd_pct
            
            if i > 0:
                prev_eq = self.equity_curve[i - 1]["equity"]
                ret = (eq - prev_eq) / prev_eq if prev_eq > 0 else 0.0
                returns.append(ret)

        # Annualized Sharpe Ratio approximation (assuming 1-min intervals, 375 mins/day, 252 days/yr)
        sharpe_ratio = 0.0
        if returns and len(returns) > 1:
            mean_ret = sum(returns) / len(returns)
            variance = sum((r - mean_ret) ** 2 for r in returns) / (len(returns) - 1)
            std_dev = math.sqrt(variance)
            if std_dev > 1e-9:
                # Annualization factor for minutes: sqrt(252 * 375) ~ 307
                sharpe_ratio = round((mean_ret / std_dev) * math.sqrt(252 * 375), 2)

        return {
            "strategy": self.strategy.name,
            "parameters": self.strategy.params,
            "initial_capital": self.initial_capital,
            "final_equity": round(final_equity, 2),
            "net_pnl": round(total_net_pnl, 2),
            "return_pct": round(total_return_pct, 2),
            "total_trades": total_trades,
            "winning_trades": len(winning_trades),
            "losing_trades": len(losing_trades),
            "win_rate_pct": round(win_rate, 2),
            "profit_factor": profit_factor,
            "max_drawdown": round(max_dd, 2),
            "max_drawdown_pct": round(max_dd_pct, 2),
            "sharpe_ratio": sharpe_ratio,
            "trades": [t.to_dict() for t in self.trades[-100:]],  # Return up to last 100 trades
            "equity_curve": self.equity_curve[::max(1, len(self.equity_curve) // 500)]  # Downsample to 500 pts for chart
        }
