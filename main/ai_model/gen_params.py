#!/usr/bin/env python3
# Reads feature_quantizer.json and generates feature_params.h with scaler arrays.
# Run manually or via CMakeLists.txt at configure time.
import json
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
JSON_PATH  = os.path.join(SCRIPT_DIR, "feature_quantizer.json")
OUT_PATH   = os.path.join(SCRIPT_DIR, "feature_params.h")

with open(JSON_PATH) as f:
    data = json.load(f)

scale = data["scale"]
mins  = data["min"]
n     = len(scale)
assert len(mins) == n, "scale/min length mismatch"

def fmt_row(values, last_row):
    row = "    " + ", ".join(f"{v:.10f}f" for v in values)
    return row if last_row else row + ","

rows_per_line = 5
lines = [
    "// Auto-generated from feature_quantizer.json — do not edit manually",
    "#ifndef FEATURE_PARAMS_H",
    "#define FEATURE_PARAMS_H",
    "",
    f"#define FEAT_COUNT {n}",
    "",
    f"static const float FEAT_SCALE[FEAT_COUNT] = {{",
]
for i in range(0, n, rows_per_line):
    lines.append(fmt_row(scale[i:i+rows_per_line], i + rows_per_line >= n))
lines += ["};", "", f"static const float FEAT_MIN[FEAT_COUNT] = {{"]
for i in range(0, n, rows_per_line):
    lines.append(fmt_row(mins[i:i+rows_per_line], i + rows_per_line >= n))
lines += ["};", "", "#endif // FEATURE_PARAMS_H", ""]

with open(OUT_PATH, "w", newline="\n") as f:
    f.write("\n".join(lines))

print(f"[gen_params] Generated {OUT_PATH}")
