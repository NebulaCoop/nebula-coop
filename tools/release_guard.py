#!/usr/bin/env python3
"""release_guard.py --repo OWNER/NAME --commit SHA [--report-only]: whether a release may be built from a commit.

A release is built only from a commit main holds (main moves only through pull requests) whose "Build result" check
(build.yml, the check main requires) on GitHub ended green: release.yml's first job runs this and every package job needs
that job (P-023). It asks GitHub through the gh command (GH_TOKEN): the comparison of the commit with main, and the
commit's "Build result" check runs.
Exit 0 when both hold; 1 with the reason otherwise. --report-only prints the answer and exits 0 (a dry run builds
packages from any branch)."""
import argparse
import json
import subprocess
import sys

# The job of build.yml that main's ruleset requires (it gates on the Windows build, the importer and the installer tests).
CHECK = "Build result"


def gh(*args):
    r = subprocess.run(["gh", *args], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("gh %s: %s" % (args[0], (r.stderr or r.stdout).strip()[-300:]))
    return r.stdout


def judge(repo, commit, ask=gh):
    """None when `commit` may be released, else why not. `ask(*args)` runs gh and returns its output."""
    try:
        status = json.loads(ask("api", "repos/%s/compare/%s...main" % (repo, commit)))["status"]
    except (RuntimeError, KeyError, ValueError) as e:
        return "GitHub could not compare %s with main: %s" % (commit[:9], e)
    # compare BASE...HEAD: "ahead" or "identical" when BASE (the commit) is an ancestor of HEAD (main).
    if status not in ("ahead", "identical"):
        return "main does not hold %s (compared with main: %s): a release is built from a commit on main" % (commit[:9], status)
    try:
        runs = json.loads(ask("api", "repos/%s/commits/%s/check-runs?check_name=%s&per_page=100" % (repo, commit, CHECK.replace(" ", "%20"))))
    except (RuntimeError, ValueError) as e:
        return "GitHub could not list the %s checks of %s: %s" % (CHECK, commit[:9], e)
    ended = [(r.get("status"), r.get("conclusion")) for r in runs.get("check_runs", [])]
    if any(s == "completed" and c == "success" for s, c in ended):
        return None
    if not ended:
        return "%s has no %s check: a release is built from a commit whose %s passed" % (commit[:9], CHECK, CHECK)
    return "no %s check of %s passed (%s)" % (CHECK, commit[:9], ", ".join("%s/%s" % (s, c) for s, c in ended))


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--repo", required=True)
    p.add_argument("--commit", required=True)
    p.add_argument("--report-only", action="store_true")
    a = p.parse_args()
    why = judge(a.repo, a.commit)
    if why is None:
        print("release_guard: %s is on main and its %s check passed" % (a.commit[:9], CHECK))
        return 0
    print("release_guard: %s%s" % (why, " (a dry run: reported only)" if a.report_only else ""))
    return 0 if a.report_only else 1


if __name__ == "__main__":
    sys.exit(main())
