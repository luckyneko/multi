#!/usr/bin/env python3
"""
report.py — run bench-multi and print a speedup summary table.

Usage:
    python3 bench/report.py [--exec <path>] [bench-multi args...]

    --exec <path>   Path to bench-multi binary (default: ./build/bench-multi)

Any remaining args are forwarded to bench-multi, so filtering and sample
count work as normal:

    python3 bench/report.py "[fast]"
    python3 bench/report.py "[bench]" --benchmark-samples=30
    python3 bench/report.py --exec build-release/bench-multi "[bench]"

An XML reporter is appended silently alongside whatever reporters the user
already requested (Catch2 v3 supports multiple simultaneous reporters).
"""

import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from typing import List, Optional, Tuple

DEFAULT_EXEC = "./build/bench-multi"


def is_baseline(name: str) -> bool:
    """Treat any benchmark whose name starts with 'serial' as the per-
    group baseline. Catches both the canonical 'serial (baseline)' used
    by tiny_tasks/mandelbrot/nested and the descriptive forms like
    'serial std::sort' / 'serial std::accumulate' / 'serial
    std::transform_reduce' used by the reduce/sort benches."""
    return name.startswith("serial")


# ---------------------------------------------------------------------------
# Formatting helpers
# ---------------------------------------------------------------------------

def fmt_time(ns: float) -> str:
    if ns >= 1e9:
        return f"{ns / 1e9:.3f} s"
    if ns >= 1e6:
        return f"{ns / 1e6:.3f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.1f} us"
    return f"{ns:.1f} ns"


def fmt_speedup(ratio: Optional[float]) -> str:
    if ratio is None:
        return ""
    return f"{ratio:.2f}x"


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

def split_args(argv: List[str]) -> Tuple[str, List[str]]:
    exec_path = DEFAULT_EXEC
    rest: List[str] = []
    i = 0
    while i < len(argv):
        if argv[i] == "--exec":
            if i + 1 >= len(argv):
                sys.exit("error: --exec requires a path argument")
            exec_path = argv[i + 1]
            i += 2
        else:
            rest.append(argv[i])
            i += 1
    return exec_path, rest


# ---------------------------------------------------------------------------
# Run bench-multi, capturing XML output via an extra reporter
# ---------------------------------------------------------------------------

def run_bench(exec_path: str, bench_args: list[str], xml_path: str) -> None:
    # Catch2 v3 multi-reporter syntax: each --reporter arg is independent;
    # xml::out=<file> routes only the XML output to our temp file without
    # disturbing any other reporters the user may have requested.
    cmd = [exec_path] + bench_args + [f"--reporter", f"xml::out={xml_path}"]
    result = subprocess.run(cmd)
    # Catch2 exits 1 when there are test failures; still parse the XML.
    if result.returncode > 1:
        sys.exit(result.returncode)


# ---------------------------------------------------------------------------
# XML parsing
# ---------------------------------------------------------------------------

Benchmark = Tuple[str, float, float]              # (name, mean_ns, sd_ns)
Row = Tuple[str, Optional[str], List[Benchmark]]  # (tc_name, section_name, benchmarks)


def parse_xml(xml_path: str) -> List[Row]:
    tree = ET.parse(xml_path)
    root = tree.getroot()  # <Catch2TestRun>
    rows: List[Row] = []

    for tc in root.findall("TestCase"):
        tc_name = tc.get("name", "")

        def extract(node: ET.Element) -> List[Benchmark]:
            out = []
            for br in node.findall("BenchmarkResults"):
                mean_el = br.find("mean")
                sd_el   = br.find("standardDeviation")
                if mean_el is not None:
                    out.append((
                        br.get("name", ""),
                        float(mean_el.get("value", 0)),
                        float(sd_el.get("value", 0)) if sd_el is not None else 0.0,
                    ))
            return out

        sections = tc.findall("Section")
        if sections:
            for sec in sections:
                benchmarks = extract(sec)
                if benchmarks:
                    rows.append((tc_name, sec.get("name"), benchmarks))
        else:
            benchmarks = extract(tc)
            if benchmarks:
                rows.append((tc_name, None, benchmarks))

    return rows


# ---------------------------------------------------------------------------
# Report printing
# ---------------------------------------------------------------------------

def print_report(rows: List[Row]) -> None:
    W_WL      = 42
    W_VARIANT = 40
    W_MEAN    = 10
    W_SD      = 10
    W_SPEEDUP =  8
    sep = "-" * (W_WL + W_VARIANT + W_MEAN + W_SD + W_SPEEDUP + 10)

    header = (
        f"{'WORKLOAD':<{W_WL}}  {'VARIANT':<{W_VARIANT}}"
        f"  {'MEAN':>{W_MEAN}}  {'±SD':>{W_SD}}  {'SPEEDUP':>{W_SPEEDUP}}"
    )
    print()
    print(header)
    print(sep)

    for tc_name, sec_name, benchmarks in rows:
        label = tc_name if sec_name is None else f"{tc_name} / {sec_name}"

        baseline_ns = next(
            (mean for name, mean, _ in benchmarks if is_baseline(name)),
            None,
        )

        first = True
        for name, mean_ns, sd_ns in benchmarks:
            wl_col = label if first else ""
            first = False

            if is_baseline(name):
                speedup = fmt_speedup(1.0)
            elif baseline_ns:
                speedup = fmt_speedup(baseline_ns / mean_ns)
            else:
                speedup = ""

            print(
                f"{wl_col:<{W_WL}}  {name:<{W_VARIANT}}"
                f"  {fmt_time(mean_ns):>{W_MEAN}}  {fmt_time(sd_ns):>{W_SD}}"
                f"  {speedup:>{W_SPEEDUP}}"
            )

        print()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    exec_path, bench_args = split_args(sys.argv[1:])

    if not os.path.isfile(exec_path):
        sys.exit(
            f"error: bench-multi not found at '{exec_path}'\n"
            f"  Build with:    cmake --build build\n"
            f"  Or specify:    python3 bench/report.py --exec <path>"
        )

    with tempfile.NamedTemporaryFile(suffix=".xml", delete=False) as f:
        xml_path = f.name

    try:
        run_bench(exec_path, bench_args, xml_path)
        rows = parse_xml(xml_path)
        print_report(rows)
    finally:
        try:
            os.unlink(xml_path)
        except OSError:
            pass


if __name__ == "__main__":
    main()
