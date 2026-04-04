"""
LIGTAS — Response Time Chart Generator (v7.0)
==============================================
Run this script AFTER you fill in the Excel template.

Usage:
    python ligtas_response_time_chart.py

Input:  ligtas_response_time_template.xlsx  (must be in same folder)
Output: response_time_chart.png             (ready for your thesis)

Requires: pip install openpyxl pandas matplotlib

Conditions (v7.0 — 2-Class, no Warning):
    0V   — Safe baseline
    35V  — Dangerous (sustained leakage)
"""

import pandas as pd
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from openpyxl import load_workbook
import sys
import os

EXCEL_FILE = "ligtas_response_time_template.xlsx"

# ── Load data from Excel ──────────────────────────────────────
if not os.path.exists(EXCEL_FILE):
    print(f"ERROR: '{EXCEL_FILE}' not found. Make sure it is in the same folder.")
    sys.exit(1)

wb = load_workbook(EXCEL_FILE, data_only=True)
ws = wb['Response Time Data']

def read_trials(row_start, row_end):
    """Read Stage1, Stage2, Stage3, Total from rows row_start to row_end."""
    records = []
    for r in range(row_start, row_end + 1):
        s1 = ws.cell(r, 4).value
        s2 = ws.cell(r, 5).value
        s3 = ws.cell(r, 6).value
        tt = ws.cell(r, 7).value
        if s1 is not None and s2 is not None and s3 is not None:
            records.append({
                'stage1': float(s1),
                'stage2': float(s2),
                'stage3': float(s3),
                'total':  float(tt) if tt is not None else float(s1)+float(s2)+float(s3)
            })
    return pd.DataFrame(records)

# Row ranges (update these to match your Excel template layout)
df_0v  = read_trials(6,  10)   # 5 trials  — 0V  Safe baseline
df_35v = read_trials(13, 27)   # 15 trials — 35V Dangerous (sustained)

for label, df in [("0V", df_0v), ("35V", df_35v)]:
    if df.empty:
        print(f"WARNING: No data found for {label}. Fill in the yellow cells in the Excel file first.")

# Allow partial data — only plot what exists
all_data = {}
if not df_0v.empty:  all_data['0V\n(Safe)']      = df_0v
if not df_35v.empty: all_data['35V\n(Dangerous)'] = df_35v

if not all_data:
    print("No data found at all. Please fill in the Excel template first.")
    sys.exit(1)

conditions  = list(all_data.keys())
cond_colors = {'0V\n(Safe)': '#22c55e', '35V\n(Dangerous)': '#f87171'}

# ── Build Figure ──────────────────────────────────────────────
fig = plt.figure(figsize=(18, 10))
fig.patch.set_facecolor('#0d1117')
gs  = gridspec.GridSpec(2, 3, figure=fig, hspace=0.45, wspace=0.35)

# ── Plot 1: Grouped bar — Stage breakdown per condition ───────
ax1 = fig.add_subplot(gs[0, :2])
ax1.set_facecolor('#161b22')

stage_labels = ['Stage 1\nSensor→Packet', 'Stage 2\nML Classify', 'Stage 3\nDisplay Update']
stage_colors = ['#3b82f6', '#f59e0b', '#8b5cf6']
x     = np.arange(len(conditions))
width = 0.25

for si, (slabel, scolor) in enumerate(zip(stage_labels, stage_colors)):
    means = []
    errs  = []
    for cond in conditions:
        df  = all_data[cond]
        col = ['stage1', 'stage2', 'stage3'][si]
        means.append(df[col].mean())
        errs.append(df[col].std() if len(df) > 1 else 0)
    bars = ax1.bar(x + si * width, means, width,
                   label=slabel, color=scolor, alpha=0.85,
                   yerr=errs, capsize=4,
                   error_kw={'ecolor': 'white', 'alpha': 0.6})
    for bar, mean, err in zip(bars, means, errs):
        ax1.text(bar.get_x() + bar.get_width() / 2,
                 bar.get_height() + max(errs) * 0.1 + 0.2,
                 f'{mean:.1f}', ha='center', va='bottom',
                 color='white', fontsize=8)

ax1.set_xticks(x + width)
ax1.set_xticklabels(conditions, color='#e5e7eb', fontsize=11)
ax1.set_ylabel('Time (ms)', color='#9ca3af', fontsize=11)
ax1.set_title('Response Time by Stage and Voltage Condition',
              color='#f9fafb', fontsize=12, fontweight='bold', pad=10)
ax1.tick_params(colors='#6b7280')
for sp in ax1.spines.values(): sp.set_color('#30363d')
ax1.legend(fontsize=9, facecolor='#21262d', labelcolor='white', loc='upper left')
ax1.grid(axis='y', color='#21262d', linewidth=0.8, linestyle='--')

# ── Plot 2: Total response time boxplot ───────────────────────
ax2 = fig.add_subplot(gs[0, 2])
ax2.set_facecolor('#161b22')

box_data   = [all_data[c]['total'].values for c in conditions]
box_colors = [cond_colors[c] for c in conditions]
bp = ax2.boxplot(box_data, patch_artist=True, notch=False,
                 medianprops=dict(color='white', linewidth=2),
                 whiskerprops=dict(color='#6b7280'),
                 capprops=dict(color='#6b7280'),
                 flierprops=dict(marker='o', color='#f87171', markersize=4))
for patch, color in zip(bp['boxes'], box_colors):
    patch.set_facecolor(color)
    patch.set_alpha(0.7)

ax2.set_xticks(range(1, len(conditions) + 1))
ax2.set_xticklabels(conditions, color='#e5e7eb', fontsize=9)
ax2.set_ylabel('Total Time (ms)', color='#9ca3af', fontsize=10)
ax2.set_title('Total Response\nDistribution', color='#f9fafb',
              fontsize=12, fontweight='bold', pad=10)
ax2.tick_params(colors='#6b7280')
for sp in ax2.spines.values(): sp.set_color('#30363d')
ax2.grid(axis='y', color='#21262d', linewidth=0.8, linestyle='--')

# ── Plot 3: Trial-by-trial line for Dangerous condition ───────
ax3 = fig.add_subplot(gs[1, :2])
ax3.set_facecolor('#161b22')

danger_key = '35V\n(Dangerous)'
if danger_key in all_data:
    df35   = all_data[danger_key]
    trials = range(1, len(df35) + 1)
    ax3.fill_between(trials, df35['total'], alpha=0.15, color='#f87171')
    ax3.plot(trials, df35['stage1'], 'o-', color='#3b82f6',
             linewidth=1.5, markersize=5, label='Stage 1: Sensor→Packet')
    ax3.plot(trials, df35['stage2'], 's-', color='#f59e0b',
             linewidth=1.5, markersize=5, label='Stage 2: ML Classify')
    ax3.plot(trials, df35['stage3'], '^-', color='#8b5cf6',
             linewidth=1.5, markersize=5, label='Stage 3: Display Update')
    ax3.plot(trials, df35['total'],  'D-', color='#f87171',
             linewidth=2,   markersize=6, label='Total', zorder=5)
    ax3.axhline(df35['total'].mean(), color='white', linestyle='--',
                linewidth=1, alpha=0.5,
                label=f"Mean total = {df35['total'].mean():.1f} ms")
    ax3.set_xlabel('Trial Number', color='#9ca3af', fontsize=11)
    ax3.set_ylabel('Time (ms)', color='#9ca3af', fontsize=11)
    ax3.set_title('Trial-by-Trial Breakdown — 35V Dangerous Condition (15 Trials)',
                  color='#f9fafb', fontsize=12, fontweight='bold', pad=10)
    ax3.set_xticks(list(trials))
    ax3.tick_params(colors='#6b7280')
    for sp in ax3.spines.values(): sp.set_color('#30363d')
    ax3.legend(fontsize=8, facecolor='#21262d', labelcolor='white', ncol=3)
    ax3.grid(color='#21262d', linewidth=0.8, linestyle='--')

# ── Plot 4: Summary stats table ───────────────────────────────
ax4 = fig.add_subplot(gs[1, 2])
ax4.set_facecolor('#161b22')
ax4.axis('off')

all_totals = pd.concat([all_data[c]['total'] for c in conditions])

stat_rows = [
    ('Mean (ms)',   lambda d: f"{d['total'].mean():.2f}"),
    ('Median (ms)', lambda d: f"{d['total'].median():.2f}"),
    ('Min (ms)',    lambda d: f"{d['total'].min():.2f}"),
    ('Max (ms)',    lambda d: f"{d['total'].max():.2f}"),
    ('Std Dev',     lambda d: f"{d['total'].std():.2f}" if len(d) > 1 else "—"),
    ('Trials',      lambda d: str(len(d))),
]

rows = [['Metric'] + [c.replace('\n', ' ') for c in conditions] + ['Overall']]
for label, fn in stat_rows:
    row_vals = [label]
    for c in conditions:
        row_vals.append(fn(all_data[c]))
    row_vals.append(fn(pd.DataFrame({'total': all_totals})))
    rows.append(row_vals)

n_cols = len(rows[0])
table  = ax4.table(cellText=rows[1:], colLabels=rows[0],
                   cellLoc='center', loc='center',
                   bbox=[0, 0, 1, 1])
table.auto_set_font_size(False)
table.set_fontsize(8.5)

for (r, c), cell in table.get_celld().items():
    if r == 0:
        cell.set_facecolor('#1D4ED8')
        cell.set_text_props(color='white', fontweight='bold')
    elif c == 0:
        cell.set_facecolor('#1c2938')
        cell.set_text_props(color='#9ca3af')
    elif c == n_cols - 1:
        cell.set_facecolor('#1A1400')
        cell.set_text_props(color='#fcd34d', fontweight='bold')
    else:
        cell.set_facecolor('#161b22' if r % 2 == 0 else '#1c2938')
        cell.set_text_props(color='white')
    cell.set_edgecolor('#30363d')

ax4.set_title('Summary Statistics', color='#f9fafb',
              fontsize=11, fontweight='bold', pad=10)

fig.suptitle('LIGTAS — Prototype Response Time Analysis (v7.0, 0-250V, Spike-Aware)',
             color='#f9fafb', fontsize=15, fontweight='bold', y=1.01)

plt.savefig('response_time_chart.png', dpi=150,
            bbox_inches='tight', facecolor='#0d1117')
print("✅ Chart saved: response_time_chart.png")
print(f"\nSummary:")
for cond in conditions:
    df = all_data[cond]
    print(f"  {cond.replace(chr(10), ' '):<22} mean={df['total'].mean():.2f}ms  "
          f"min={df['total'].min():.2f}ms  max={df['total'].max():.2f}ms")
