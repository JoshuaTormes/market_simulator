#!/usr/bin/env python3
"""
analyze_run.py — Quantitative diagnostic for simulacao_mercado_3 CSV exports.

Usage:
    python3 scripts/analyze_run.py <csv_dir>

Expects:
    <csv_dir>/snapshots.csv   — tick,mid_price,spread,...,ofi,...,regime
    <csv_dir>/trades.csv      — tick,seq_no,price,qty,taker_side,...

Reports (all referenced in spec 2026-05-23_mercado-endogeno):
  - Fundamental contamination: % ticks where spread==0 (mid set to fundamental)
  - Return provenance: % returns from fundamental-anchored ticks
  - ACF of r, |r|, OFI (with ±1.96/√N significance bands)
  - Kurtosis / skew of returns
  - Price trajectory summary
  - Trade size distribution (mean, std, percentiles)
  - Price impact proxy: bucket-average |Δmid| vs √volume
  - Regime changes count
"""

import sys
import csv
import math
import statistics
from collections import defaultdict


# ── I/O helpers ───────────────────────────────────────────────────────────────

def read_csv(path):
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        return list(reader)


# ── Statistics ────────────────────────────────────────────────────────────────

def acf(series, max_lag=20):
    n = len(series)
    if n < 2:
        return []
    mean = statistics.mean(series)
    var = statistics.variance(series)
    if var == 0:
        return [0.0] * max_lag
    centered = [x - mean for x in series]
    acf_vals = []
    for lag in range(1, max_lag + 1):
        cov = sum(centered[i] * centered[i + lag] for i in range(n - lag)) / n
        acf_vals.append(cov / var)
    return acf_vals


def kurtosis_excess(series):
    n = len(series)
    if n < 4:
        return float("nan")
    mean = statistics.mean(series)
    std = statistics.stdev(series)
    if std == 0:
        return float("nan")
    m4 = sum((x - mean) ** 4 for x in series) / n
    m2 = std ** 2
    return m4 / (m2 ** 2) - 3.0


def skewness(series):
    n = len(series)
    if n < 3:
        return float("nan")
    mean = statistics.mean(series)
    std = statistics.stdev(series)
    if std == 0:
        return float("nan")
    m3 = sum((x - mean) ** 3 for x in series) / n
    return m3 / (std ** 3)


def percentile(sorted_series, p):
    n = len(sorted_series)
    if n == 0:
        return float("nan")
    idx = (p / 100.0) * (n - 1)
    lo = int(idx)
    hi = min(lo + 1, n - 1)
    frac = idx - lo
    return sorted_series[lo] * (1 - frac) + sorted_series[hi] * frac


def hill_estimator(sorted_series, k):
    """Hill estimator for tail index using top-k observations (sorted descending)."""
    if k < 2 or k >= len(sorted_series):
        return float("nan")
    x_k = sorted_series[k - 1]
    if x_k <= 0:
        return float("nan")
    total = sum(math.log(sorted_series[i] / x_k) for i in range(k))
    if total == 0:
        return float("nan")
    return k / total


# ── Main diagnostic ───────────────────────────────────────────────────────────

def run(csv_dir):
    snaps = read_csv(f"{csv_dir}/snapshots.csv")
    trades = read_csv(f"{csv_dir}/trades.csv")

    n_snaps = len(snaps)
    n_trades = len(trades)

    print(f"=== analyze_run diagnostic ===")
    print(f"Snapshots: {n_snaps}  |  Trades: {n_trades}")
    print()

    # ── Contamination: spread==0 ticks ───────────────────────────────────────
    zero_spread_ticks = sum(1 for s in snaps if float(s["spread"]) == 0)
    contamination_pct = 100.0 * zero_spread_ticks / n_snaps if n_snaps else 0
    print(f"[CONTAMINATION] spread==0 ticks: {zero_spread_ticks}/{n_snaps} = {contamination_pct:.1f}%")
    print(f"  -> mid set to fundamental on these ticks (target: 0%)")
    print()

    # ── Returns from valid ticks only ────────────────────────────────────────
    mid_prices = [float(s["mid_price"]) for s in snaps]
    spreads_arr = [float(s["spread"]) for s in snaps]

    returns_all = []
    returns_from_contaminated = 0
    for i in range(1, len(mid_prices)):
        if mid_prices[i] > 0 and mid_prices[i - 1] > 0:
            r = math.log(mid_prices[i] / mid_prices[i - 1])
            returns_all.append(r)
            if spreads_arr[i] == 0 or spreads_arr[i - 1] == 0:
                returns_from_contaminated += 1

    n_ret = len(returns_all)
    ret_contamination_pct = 100.0 * returns_from_contaminated / n_ret if n_ret else 0
    print(f"[RETURNS] n={n_ret}, mean={statistics.mean(returns_all):.4e}, "
          f"std={statistics.stdev(returns_all):.4e}")
    print(f"  Returns touching contaminated tick: {returns_from_contaminated} ({ret_contamination_pct:.1f}%)")
    print()

    # ── Price trajectory ──────────────────────────────────────────────────────
    p0 = mid_prices[0]
    p_last = mid_prices[-1]
    pct_change = 100.0 * (p_last - p0) / p0 if p0 else 0
    print(f"[PRICE TRAJECTORY] start={p0:.0f}, end={p_last:.0f}, change={pct_change:+.1f}%")
    print()

    # ── ACF of returns, |returns|, OFI ──────────────────────────────────────
    sig_band = 1.96 / math.sqrt(n_ret) if n_ret > 0 else 0
    print(f"[ACF] significance band: ±{sig_band:.4f} (1.96/√{n_ret})")

    max_lag = 20
    acf_r    = acf(returns_all, max_lag)
    acf_absr = acf([abs(r) for r in returns_all], max_lag)

    ofi_col = "ofi_tick" if "ofi_tick" in snaps[0] else "ofi"
    ofi_series = [float(s[ofi_col]) for s in snaps if float(s[ofi_col]) != 0]
    acf_ofi  = acf(ofi_series, max_lag) if len(ofi_series) > 2 else []

    print(f"  ACF(r,  lag 1..5): {[f'{v:.4f}' for v in acf_r[:5]]}")
    print(f"  ACF(|r|,lag 1..5): {[f'{v:.4f}' for v in acf_absr[:5]]}")
    if acf_ofi:
        print(f"  ACF(OFI,lag 1..5): {[f'{v:.4f}' for v in acf_ofi[:5]]}")
    print()

    # ── Kurtosis and skew ─────────────────────────────────────────────────────
    kurt = kurtosis_excess(returns_all)
    skew = skewness(returns_all)
    print(f"[MOMENTS] excess kurtosis={kurt:.3f} (target >1), skew={skew:.3f} (target <0)")
    print()

    # ── Regime changes ────────────────────────────────────────────────────────
    regime_vals = [int(s["regime"]) for s in snaps]
    regime_changes = sum(1 for i in range(1, len(regime_vals))
                         if regime_vals[i] != regime_vals[i - 1])
    print(f"[REGIME] changes in log: {regime_changes}")
    print()

    # ── Trade size distribution ───────────────────────────────────────────────
    qtys = sorted([float(t["qty"]) for t in trades])
    if qtys:
        mean_qty = statistics.mean(qtys)
        std_qty = statistics.stdev(qtys) if len(qtys) > 1 else 0
        p50 = percentile(qtys, 50)
        p95 = percentile(qtys, 95)
        p99 = percentile(qtys, 99)
        print(f"[TRADE SIZES] n={n_trades}, mean={mean_qty:.1f}, std={std_qty:.1f}, "
              f"p50={p50:.0f}, p95={p95:.0f}, p99={p99:.0f}")

        # Hill estimator on top tail
        for k in [50, 100, 200]:
            if len(qtys) > k:
                top_k = sorted(qtys, reverse=True)
                alpha = hill_estimator(top_k, k)
                print(f"  Hill α (k={k}): {alpha:.3f}")
    print()

    # ── Price impact: |Δmid| vs √volume ──────────────────────────────────────
    # Group trades by tick, compute total volume per tick and |Δmid| per tick
    volume_by_tick = defaultdict(float)
    for t in trades:
        volume_by_tick[int(t["tick"])] += float(t["qty"])

    mid_by_tick = {int(s["tick"]): float(s["mid_price"]) for s in snaps}
    ticks_sorted = sorted(mid_by_tick.keys())

    impact_pairs = []
    for i in range(1, len(ticks_sorted)):
        t_prev = ticks_sorted[i - 1]
        t_curr = ticks_sorted[i]
        if t_curr not in volume_by_tick:
            continue
        vol = volume_by_tick[t_curr]
        p_prev = mid_by_tick[t_prev]
        p_curr = mid_by_tick[t_curr]
        if p_prev > 0 and vol > 0:
            impact = abs(math.log(p_curr / p_prev))
            impact_pairs.append((vol, impact))

    if impact_pairs:
        # Bucket by √vol quintiles
        sqrt_vols = sorted([(math.sqrt(v), imp) for v, imp in impact_pairs])
        n_buckets = 5
        bucket_size = max(1, len(sqrt_vols) // n_buckets)
        print("[PRICE IMPACT] |Δmid| vs √volume buckets (low→high vol):")
        for b in range(n_buckets):
            bucket = sqrt_vols[b * bucket_size:(b + 1) * bucket_size]
            if not bucket:
                continue
            avg_svol = statistics.mean(x[0] for x in bucket)
            avg_imp = statistics.mean(x[1] for x in bucket)
            print(f"  √vol={avg_svol:.1f} → |Δmid|={avg_imp:.5f}")
    print()

    print("=== End of diagnostic ===")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 scripts/analyze_run.py <csv_dir>", file=sys.stderr)
        sys.exit(1)
    run(sys.argv[1])
