#!/usr/bin/env python3
"""Summarize GCC coverage when lcov/genhtml are not installed."""

import argparse
import gzip
import html
import json
from pathlib import Path
import re
import subprocess
import tempfile


BOOSTIO_EXCLUDED = (
    "/interceptor/", "/io_interceptor/", "/security/openssl_tools/",
    "/cluster/", "/net/", "/sdk/", "/server/", "/underfs/",
    "/cache/write/",
)
BOOSTIO_EXCLUDED_FILES = {
    "/disk/common/ngx_rbtree.c",
    "/security/expiration_check/expire_checker.cpp",
}
BOOSTIO_BRANCH_EXCLUDE = re.compile(
    r"LCOV_EXCL_BR_LINE|^\s*(?:NET_LOG|CLIENT_LOG|LOG|BIO_TP_START|ChkTrue)"
)
KV_BRANCH_EXCLUDE = re.compile(
    r"LCOV_EXCL_BR_LINE|DL_LOAD_SYM|LOG_\w*|UBSIO_KVC_ASSERT_\w*|std::cout"
)


def included(source, source_root, project):
    try:
        relative = "/" + source.relative_to(source_root).as_posix()
    except ValueError:
        return None
    if project == "boostio":
        if relative in BOOSTIO_EXCLUDED_FILES or any(part in relative for part in BOOSTIO_EXCLUDED):
            return None
        return relative
    path = source.relative_to(source_root)
    if path.match("csrc/*/*.cpp") or path.match("csrc/utils/*.h"):
        return relative
    return None


def collect(build_dir, source_root, project):
    line_hits = {}
    branch_hits = {}
    branch_exclude = BOOSTIO_BRANCH_EXCLUDE if project == "boostio" else KV_BRANCH_EXCLUDE
    gcno_files = [path for path in build_dir.rglob("*.gcno") if not any(
        part in ("_deps", "3rdparty") for part in path.parts)]
    if not gcno_files:
        raise RuntimeError("No .gcno files found in " + str(build_dir))
    with tempfile.TemporaryDirectory(prefix="ubsio-gcov-") as temp_name:
        temp = Path(temp_name)
        for gcno in gcno_files:
            result = subprocess.run(
                ["gcov", "-j", "-b", str(gcno)], cwd=temp,
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, check=False,
            )
            if result.returncode:
                raise RuntimeError("gcov failed for {}: {}".format(gcno, result.stderr.strip()))
            for report in temp.glob("*.gcov.json.gz"):
                with gzip.open(report, "rt", encoding="utf-8") as stream:
                    data = json.load(stream)
                report.unlink()
                workdir = Path(data.get("current_working_directory", build_dir))
                for entry in data.get("files", []):
                    source = Path(entry["file"])
                    if not source.is_absolute():
                        source = workdir / source
                    source = source.resolve()
                    relative = included(source, source_root, project)
                    if relative is None:
                        continue
                    lines = line_hits.setdefault(relative, {})
                    branches = branch_hits.setdefault(relative, {})
                    source_lines = source.read_text(encoding="utf-8", errors="replace").splitlines()
                    for row in entry.get("lines", []):
                        number = row["line_number"]
                        lines[number] = max(lines.get(number, 0), int(row["count"]))
                        text = source_lines[number - 1] if number <= len(source_lines) else ""
                        if branch_exclude.search(text):
                            continue
                        for index, branch in enumerate(row.get("branches", [])):
                            key = (number, index)
                            branches[key] = max(branches.get(key, 0), int(branch["count"]))
    return line_hits, branch_hits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", choices=("boostio", "kv"), required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--min-line", type=float, required=True)
    parser.add_argument("--min-branch", type=float, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    line_hits, branch_hits = collect(args.build.resolve(), root / "src", args.project)
    line_total = sum(len(rows) for rows in line_hits.values())
    line_covered = sum(sum(value > 0 for value in rows.values()) for rows in line_hits.values())
    branch_total = sum(len(rows) for rows in branch_hits.values())
    branch_covered = sum(sum(value > 0 for value in rows.values()) for rows in branch_hits.values())
    if not line_total or not branch_total:
        raise RuntimeError("No production line or branch coverage data found")
    line_percent = 100 * line_covered / line_total
    branch_percent = 100 * branch_covered / branch_total
    summary = (
        "lines......: {:.2f}% ({} of {} lines)\n"
        "branches...: {:.2f}% ({} of {} branches)"
    ).format(line_percent, line_covered, line_total,
             branch_percent, branch_covered, branch_total)
    args.report.mkdir(parents=True, exist_ok=True)
    (args.report / "gcov-summary.txt").write_text(summary + "\n", encoding="utf-8")
    rows = []
    for name, counts in sorted(line_hits.items()):
        covered = sum(value > 0 for value in counts.values())
        rows.append("<tr><td>{}</td><td>{}/{}</td></tr>".format(
            html.escape(name), covered, len(counts)))
    (args.report / "index.html").write_text(
        "<!doctype html><meta charset='utf-8'><title>Coverage</title>"
        "<h1>Coverage</h1><pre>{}</pre><table>{}</table>".format(
            html.escape(summary), "".join(rows)), encoding="utf-8")
    print(summary)
    if line_percent <= args.min_line or branch_percent <= args.min_branch:
        raise RuntimeError("Coverage must exceed {:.0f}% lines and {:.0f}% branches".format(
            args.min_line, args.min_branch))


if __name__ == "__main__":
    main()
