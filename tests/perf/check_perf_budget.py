#!/usr/bin/env python3
import argparse
import json
import sys


def load_metrics(path):
    with open(path, "r", encoding="utf-8") as fh:
        root = json.load(fh)
    metrics = {}
    for item in root.get("metrics", []):
        name = item.get("name")
        if name:
            metrics[name] = float(item.get("value", 0.0))
    return root, metrics


def load_budget(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def check_absolute(current, budget):
    failures = []
    for name, rule in budget.get("absolute", {}).items():
        if name not in current:
            failures.append(f"{name}: missing from current metrics")
            continue
        limit = float(rule["max"])
        value = current[name]
        if value > limit:
            failures.append(f"{name}: {value:.3f} > absolute budget {limit:.3f}")
    return failures


def check_relative(baseline, current, budget):
    failures = []
    for name, rule in budget.get("relative", {}).items():
        if name not in baseline:
            continue
        if name not in current:
            failures.append(f"{name}: missing from current metrics")
            continue
        base = baseline[name]
        value = current[name]
        percent = float(rule["max_regression_percent"])
        min_delta = float(rule.get("min_absolute_delta", 0.0))
        limit = base * (1.0 + percent / 100.0) + min_delta
        if value > limit:
            failures.append(
                f"{name}: {value:.3f} > baseline {base:.3f} "
                f"+ {percent:.1f}% + {min_delta:.3f} = {limit:.3f}"
            )
    return failures


def main():
    parser = argparse.ArgumentParser(
        description="Check Emerald perf JSON against absolute and relative budgets."
    )
    parser.add_argument("current", help="Current emerald_perf_tests JSON output")
    parser.add_argument(
        "--budget",
        default="tests/perf/perf-budget.json",
        help="Budget JSON file",
    )
    parser.add_argument(
        "--baseline",
        help="Optional baseline JSON for release-to-release regression checks",
    )
    args = parser.parse_args()

    current_root, current = load_metrics(args.current)
    budget = load_budget(args.budget)

    failures = check_absolute(current, budget)
    if args.baseline:
        _, baseline = load_metrics(args.baseline)
        failures.extend(check_relative(baseline, current, budget))

    if failures:
        print("Performance budget check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    profile = budget.get("profile", {})
    version = current_root.get("version", "unknown")
    print(
        "Performance budgets passed "
        f"(version={version}, notes={profile.get('notes', 'n/a')}, "
        f"words={profile.get('words_per_note', 'n/a')})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
