import argparse
import csv
import os
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--simulator", required=True)
    parser.add_argument("--fixture", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="marco-regression-") as tmp_dir:
        current_output = os.path.join(tmp_dir, "current_physics.csv")
        subprocess.run([args.simulator, current_output], check=True)

        with open(current_output, newline="", encoding="utf-8") as current_file:
            current_rows = list(csv.reader(current_file))
        with open(args.fixture, newline="", encoding="utf-8") as fixture_file:
            fixture_rows = list(csv.reader(fixture_file))

        if current_rows != fixture_rows:
            print("ERROR: deterministic physics fixture mismatch")
            return 1

    print(f"PASS: {len(current_rows) - 1} deterministic physics rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
