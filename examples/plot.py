"""Draw contact windows and actual reserved transmissions; requires matplotlib."""
import argparse
import csv
import io
from pathlib import Path
import subprocess

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary", default="build/contact-route")
parser.add_argument("--contacts", default="examples/contacts.csv")
parser.add_argument("--bundles", default="examples/bundles.csv")
parser.add_argument("--output", default="docs/reservations.png")
args = parser.parse_args()
with open(args.contacts) as stream:
    contacts = list(csv.DictReader(stream))
run = subprocess.run([args.binary, args.contacts, args.bundles, "--bookings"],
                     check=True, capture_output=True, text=True)
bookings = list(csv.DictReader(io.StringIO(run.stdout)))
colors = {name: color for name, color in zip(sorted({b["bundle"] for b in bookings}),
                                           ["#2166ac", "#b35806", "#1b7837", "#762a83"])}
fig, axis = plt.subplots(figsize=(10, 4.5), layout="constrained")
for row, contact in enumerate(contacts):
    start, end = float(contact["opens_us"])/1e6, float(contact["closes_us"])/1e6
    axis.barh(row, end-start, left=start, color="#dddddd", height=0.55)
    for booking in bookings:
        if booking["contact"] != contact["id"]:
            continue
        begin, finish = float(booking["starts_us"])/1e6, float(booking["finishes_us"])/1e6
        axis.barh(row, finish-begin, left=begin, color=colors[booking["bundle"]], height=0.38)
        arrival = finish + float(contact["delay_us"])/1e6
        axis.plot([finish, arrival], [row, row], color=colors[booking["bundle"]], linestyle=":")
        axis.scatter([arrival], [row], color=colors[booking["bundle"]], s=20)
axis.set_yticks(range(len(contacts)), [f'{c["id"]}: {c["from"]} → {c["to"]}' for c in contacts])
axis.invert_yaxis()
axis.set(xlabel="Elapsed time (seconds)", title="Contact windows, transmitter reservations, and arrival after light time")
axis.legend(handles=[Patch(facecolor="#dddddd", label="Available contact")]+[
    Patch(facecolor=color, label=bundle) for bundle, color in colors.items()], loc="upper right")
axis.grid(axis="x", alpha=0.2)
axis.spines[["top", "right"]].set_visible(False)
output = Path(args.output)
output.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(output, dpi=160)
