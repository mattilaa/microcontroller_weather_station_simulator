#!/usr/bin/env python3
"""Import weather data from the firmware and plot it.

The firmware samples its sensors every second and logs one record per 10
simulated minutes (means of the samples, and the rain in the interval). It
serves that log over its UDP service (port 5000 in the guest; the CMake `run`
target forwards host port 5555 to it). Simulated time runs at real time by
default; the speed switch makes it faster (600 = ten simulated minutes per
second, 3600 = an hour per second, 72000 = twenty hours per second).

    # Record live: own QEMU, collect records as they are measured, keep
    # updating the chart (Ctrl-C stops). 48 simulated hours at 3600x take
    # 48 seconds; at 1x (real time) they take 48 hours.
    python tools/weather_tool.py record --speed 3600 --hours 48
    python tools/weather_tool.py record --speed 72000 --hours 72   # ~5 s
    python tools/weather_tool.py record --speed 600 --live      # live window

    # Against an already running firmware (cmake --build build --target run):
    python tools/weather_tool.py record --port 5555
    python tools/weather_tool.py fetch --out weather.csv        # one import
    python tools/weather_tool.py plot weather.csv --out weather.png

    # Instant: fast-forward a simulated day without waiting at all.
    python tools/weather_tool.py demo --firmware build/firmware.elf --minutes 1440

`fetch` appends only records that aren't in the CSV yet, so it can be run
repeatedly. `--advance MINUTES` fast-forwards the simulation first.

The chart follows the Finnish Meteorological Institute's daily forecast chart:
temperature (red) and dew point (green, dotted) on the left axis, hourly
precipitation bars on the right axis, a dashed line at midnight, a cloud panel
on a pressure axis, and a wind panel with hourly direction arrows and speeds.
"""

import argparse
import csv
import datetime
import math
import os
import signal
import socket
import subprocess
import sys
import time

COLUMNS = ["seq", "day", "time", "temperature", "humidity", "wind_speed", "wind_direction", "rain", "cloud_cover",
           "lcl", "cirrus", "altostratus", "cumulus", "cumulus_top", "cumulonimbus",
           "cirrus_base", "cirrus_top", "altostratus_base", "altostratus_top",
           "stratus", "stratocumulus", "nimbostratus", "altocumulus", "stratus_top", "stratocumulus_top",
           "nimbostratus_top", "altocumulus_base", "altocumulus_top", "pressure", "wind_gust",
           "tornado"]
CLOUD_LAYER_COLUMNS = COLUMNS[COLUMNS.index("lcl"):COLUMNS.index("pressure")]
SCENARIOS = ["default", "summer_heat", "winter_high", "storm", "thunderstorm", "mcs", "supercell"]
SERVICE_PORT = 5000

# ---- import --------------------------------------------------------------------


class WeatherClient:
    """Talks to the firmware's UDP weather service."""

    def __init__(self, host="127.0.0.1", port=5555, timeout=1.0, retries=5):
        self.address = (host, port)
        self.retries = retries
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.settimeout(timeout)

    def command(self, text):
        """Send one command and return the reply text. UDP can drop
        datagrams, so retry on timeout."""
        for _ in range(self.retries):
            self.socket.sendto(text.encode(), self.address)
            try:
                data, _ = self.socket.recvfrom(4096)
                return data.decode()
            except socket.timeout:
                continue
        raise TimeoutError(f"No reply to {text!r} from {self.address[0]}:{self.address[1]}")

    def wait_until_ready(self, timeout=15.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                if self.command("ping").strip() == "pong":
                    return
            except TimeoutError:
                pass
        raise TimeoutError("Firmware did not answer 'ping'")

    def fetch_log(self, start=0):
        """Yield logged readings (dicts) with sequence numbers >= start."""
        next_sequence = start
        while True:
            reply = self.command(f"log {next_sequence}")
            header, *rows = reply.rstrip("\n").split("\n")
            if not header.startswith("log "):
                raise ValueError(f"Unexpected reply: {header}")
            fields = dict(item.split("=") for item in header.split()[1:])
            if rows and int(rows[0].split(",")[0]) > next_sequence:
                # The firmware keeps 256 records; at very high speeds it can
                # overwrite some before they are imported.
                missed = int(rows[0].split(",")[0]) - next_sequence
                print(f"warning: {missed} records were overwritten before import "
                      "(import more often, e.g. a smaller --interval, or lower the speed)",
                      file=sys.stderr)
            for row in rows:
                yield dict(zip(COLUMNS, row.split(",")))
            next_sequence = int(fields["next"])
            if int(fields["count"]) == 0:
                return


def last_sequence_in(path):
    if not os.path.exists(path):
        return None
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    return int(rows[-1]["seq"]) if rows else None


def fetch(client, out_path, advance=None):
    """Import new readings into `out_path`; returns how many were added."""
    if advance:
        print(client.command(f"advance {advance}").strip())
    last = last_sequence_in(out_path)
    start = 0 if last is None else last + 1
    # A file can exist with only its header (no records yet at real time).
    new_file = not os.path.exists(out_path) or os.path.getsize(out_path) == 0
    added = 0
    with open(out_path, "a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=COLUMNS)
        if new_file:
            writer.writeheader()
        for row in client.fetch_log(start):
            writer.writerow(row)
            added += 1
    return added


# ---- data preparation ----------------------------------------------------------


def load(path):
    """Read a CSV written by `fetch` into a list of readings with timestamps.
    Simulated day 1 is mapped to an arbitrary calendar date."""
    base = datetime.datetime(2026, 1, 1)
    readings = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            hours, minutes = map(int, row["time"].split(":"))
            readings.append(
                {
                    "when": base + datetime.timedelta(days=int(row["day"]) - 1, hours=hours, minutes=minutes),
                    "day": int(row["day"]),
                    "temperature": float(row["temperature"]),
                    "humidity": float(row["humidity"]),
                    "wind_speed": float(row["wind_speed"]),
                    "wind_direction": float(row["wind_direction"]),
                    "rain": float(row["rain"]),
                    # Older CSVs (before the cloud sensors) lack these columns.
                    "cloud_cover": float(row["cloud_cover"]) if row.get("cloud_cover") else None,
                    **{key: float(row[key]) if row.get(key) else None for key in CLOUD_LAYER_COLUMNS},
                    "pressure": float(row["pressure"]) if row.get("pressure") else None,
                    "wind_gust": float(row["wind_gust"]) if row.get("wind_gust") else None,
                    "tornado": row.get("tornado") == "1",
                }
            )
    readings.sort(key=lambda r: r["when"])
    return readings


def dew_point(temperature, humidity):
    """Dew point in degC from temperature (degC) and relative humidity (%),
    Magnus formula (Alduchov & Eskridge coefficients)."""
    a, b = 17.625, 243.04
    gamma = math.log(max(humidity, 1.0) / 100.0) + a * temperature / (b + temperature)
    return b * gamma / (a - gamma)


RECORD_MINUTES = 10


def hourly(readings):
    """Group 10-minute records into clock hours: precipitation sum, mean wind
    speed and vector-averaged wind direction. A record's time is the END of
    its interval, so group by the interval's start."""
    hours = {}
    for r in readings:
        key = (r["when"] - datetime.timedelta(minutes=RECORD_MINUTES)).replace(minute=0)
        h = hours.setdefault(key, {"rain": 0.0, "speeds": [], "u": 0.0, "v": 0.0})
        h["rain"] += r["rain"]
        h["speeds"].append(r["wind_speed"])
        # Direction the wind comes FROM, as a speed-weighted unit vector.
        radians = math.radians(r["wind_direction"])
        h["u"] += r["wind_speed"] * math.sin(radians)
        h["v"] += r["wind_speed"] * math.cos(radians)
    result = []
    for start in sorted(hours):
        h = hours[start]
        direction = math.degrees(math.atan2(h["u"], h["v"])) % 360
        result.append(
            {
                "start": start,
                "rain": h["rain"],
                "wind_speed": sum(h["speeds"]) / len(h["speeds"]),
                "wind_direction": direction,
            }
        )
    return result


# ---- plotting ------------------------------------------------------------------

TEMPERATURE_COLOR = "#9b1c1c"
DEW_POINT_COLOR = "#4f9a2e"
RAIN_COLOR = "#3b6fd4"
# Cloud panel: each type at its level (hPa), as in the firmware's weather_model.
# All clouds share one colour scale: the denser the cloud, the darker it is,
# from thin wispy cirrus to the towering, opaque cumulonimbus.
CLOUD_TYPES = [
    # key, label, density (optical thickness at full
    # cover), base, top: a column name (per record) or a fixed hPa
    # (fallback for older CSVs); then how soft the lower and upper edges
    # are, as fractions of the layer's depth.
    ("cirrus", "Cirrus", 0.8, ("cirrus_base", 350), ("cirrus_top", 200), 0.45, 0.45),
    ("altocumulus", "Altocumulus", 0.9, ("altocumulus_base", 600), ("altocumulus_top", 540), 0.3, 0.4),
    ("altostratus", "Altostratus", 1.2, ("altostratus_base", 700), ("altostratus_top", 450), 0.2, 0.3),
    ("cumulus", "Cumulus", 1.4, ("lcl", 900), ("cumulus_top", 700), 0.04, 0.4),
    ("stratocumulus", "Stratocumulus", 1.5, ("lcl", 950), ("stratocumulus_top", 850), 0.06, 0.4),
    ("stratus", "Stratus", 1.7, ("lcl", 950), ("stratus_top", 920), 0.1, 0.2),
    ("nimbostratus", "Nimbostratus", 2.6, ("lcl", 950), ("nimbostratus_top", 550), 0.15, 0.25),
    ("cumulonimbus", "Cumulonimbus", 4.0, ("lcl", 900), (None, 200), 0.04, 0.15),
]
CLOUD_LIGHT = "#dfdfdf"  # the thinnest visible cloud
CLOUD_DARK = "#555555"   # dense cloud cores
CLOUD_BANDS = [0.015, 0.12, 0.25, 0.45, 0.75, 1.2, 1.8, 2.6, 3.6, 5.0, 8.0]
LCL_COLOR = "#c0601a"
PRESSURE_TICKS = [1000, 850, 700, 500, 300, 200]
# Standard-atmosphere heights of those levels, for the km scale.
PRESSURE_KM = {1000: "0.1", 850: "1.5", 700: "3.0", 500: "5.6", 300: "9.2", 200: "11.8"}
WIND_COLOR = "#2f4f6f"
TORNADO_COLOR = "#c0392b"
PRESSURE_COLOR = "#7a3e9d"
GRID_COLOR = "#dddddd"


def nice_temperature_range(values, intervals_min=4):
    """Axis limits on multiples of 5 degC with room for the rain bars at the
    bottom, like the FMI chart (at least `intervals_min` gridlines apart)."""
    low = math.floor((min(values) - 1) / 5) * 5
    high = math.ceil((max(values) + 1) / 5) * 5
    while (high - low) / 5 < intervals_min:
        low -= 5
    return low, high


def smoothstep(x):
    import numpy as np

    x = np.clip(x, 0.0, 1.0)
    return x * x * (3 - 2 * x)


def cloud_colours(thickness):
    """RGBA for cloud optical thicknesses: transparent where there's none,
    then from pale grey to charcoal as the cloud gets thicker."""
    import matplotlib.colors
    import numpy as np

    darkness = 1 - np.exp(-thickness / 1.7)  # 0 clear .. 1 opaque
    light = np.array(matplotlib.colors.to_rgb(CLOUD_LIGHT))
    dark = np.array(matplotlib.colors.to_rgb(CLOUD_DARK))
    shade = np.clip((darkness - 0.1) / 0.85, 0, 1)[..., None]
    rgba = np.empty(thickness.shape + (4,))
    rgba[..., :3] = light + (dark - light) * shade
    rgba[..., 3] = np.clip(darkness * 4.0, 0, 1)  # faint cloud fades out
    return rgba


def storm_cloud_profile(times, centers, levels, readings, base, top):
    """Schematic convective cores, spreading anvils, and storm-base skirts.

    Widths are display cues on the time axis. Recorded gust strength widens
    severe-storm anvils and the low skirt; gaps split independent segments.
    """
    import numpy as np

    amount = np.clip([(r.get("cumulonimbus") or 0.0) / 100 for r in readings], 0, 1)
    if not amount.any():
        return np.zeros((len(levels), len(times)))
    peak_gust = max((r.get("wind_gust") or r.get("wind_speed") or 0.0) for r in readings)
    severe = min(max((peak_gust - 30.0) / 35.0, 0.0), 1.0)
    if any(r.get("tornado") for r in readings):
        severe = 1.0
    width = RECORD_MINUTES / 1440
    splits = np.r_[0, np.flatnonzero(np.diff(centers) > width * 1.5) + 1, len(centers)]

    def spread(signal, minutes):
        result = np.zeros(len(times))
        for start, end in zip(splits, splits[1:]):
            inside = (times >= centers[start] - width / 2) & (times <= centers[end - 1] + width / 2)
            distance = (times[inside, None] - centers[None, start:end]) / (minutes / 1440)
            weights = np.exp(-0.5 * distance ** 2)
            result[inside] = weights @ signal[start:end] / weights.sum(axis=1)
        return result[None, :]

    core = spread(amount ** 2.2, 10 + 8 * severe)
    anvil = np.clip(1.8 * spread(amount, 50 + 70 * severe), 0, 1)
    skirt = np.clip(1.25 * spread(amount, 15 + 40 * severe), 0, 1)
    depth = base - top
    safe_depth = np.where(depth > 0, depth, 1.0)
    height = (base - levels[:, None]) / safe_depth
    envelope = smoothstep(height / 0.035) * smoothstep((1 - height) / 0.12)
    anvil_weight = np.exp(-((height - 0.90) / 0.14) ** 2)
    skirt_weight = (0.65 + 0.25 * severe) * np.exp(-((height - 0.10) / 0.17) ** 2)
    body_weight = (1 - anvil_weight) * (1 - skirt_weight)
    return envelope * (body_weight * core + anvil_weight * anvil + skirt_weight * skirt) * (depth > 0)


def cloud_density(readings, rows=300):
    """Build a display field from recorded layers and schematic storm profiles.

    Layer amounts/heights interpolate between adjacent records; convective
    cores spread into anvils and low skirts. Missing intervals stay masked.
    """
    import matplotlib.dates as mdates
    import numpy as np

    width = RECORD_MINUTES / 1440
    centers = np.array([mdates.date2num(r["when"]) - width / 2 for r in readings])
    left, right = centers[0] - width / 2, centers[-1] + width / 2
    samples = max(5, min(4096, math.ceil((right - left) / width * 4) + 1))
    times = np.unique(np.r_[np.linspace(left, right, samples), centers])
    levels = np.log(np.geomspace(185.0, 1050.0, rows))
    thickness = np.zeros((rows, len(times)))

    def interpolate(values):
        return np.interp(times, centers, values)[None, :]

    for key, _, density, (base_key, base_fixed), (top_key, top_fixed), soft_base, soft_top in CLOUD_TYPES:
        amount = interpolate([min(max((r.get(key) or 0.0) / 100.0, 0.0), 1.0) for r in readings])
        base = interpolate([math.log(r.get(base_key) or base_fixed) if base_key else math.log(base_fixed)
                            for r in readings])
        top = interpolate([math.log(r.get(top_key) or top_fixed) if top_key else math.log(top_fixed)
                           for r in readings])
        if key == "cumulonimbus":
            thickness += density * storm_cloud_profile(times, centers, levels, readings, base, top)
            continue
        depth = base - top
        valid = depth > 0
        safe_depth = np.where(valid, depth, 1.0)
        above_base = (base - levels[:, None]) / (safe_depth * soft_base)
        below_top = (levels[:, None] - top) / (safe_depth * soft_top)
        profile = smoothstep(above_base) * smoothstep(below_top)
        if key == "cirrus":
            profile *= np.exp(-(((levels[:, None] - (base + top) / 2) / (safe_depth / 2.2)) ** 2))
        else:
            # A graded interior reveals overlapping layers instead of painting
            # a solid wall from cloud base to top. No synthetic weather noise.
            height = np.clip((levels[:, None] - top) / safe_depth, 0.0, 1.0)
            profile *= 0.35 + 0.65 * np.sin(np.pi * height) ** 0.8
        thickness += profile * density * amount * valid

    for previous, following in zip(centers, centers[1:]):
        if following - previous > width * 1.5:
            missing = (times > previous + width / 2) & (times < following - width / 2)
            thickness[:, missing] = np.nan
    return times, levels, thickness


def draw_cloud_panel(ax, readings):
    """Windy-style grayscale cloud contours on a log-pressure height axis.

    Layer interpolation and anvil/core/skirt shapes are rendering schematics;
    measurements, rain totals, and the simulated weather remain unchanged.
    """
    import matplotlib.dates as mdates
    import numpy as np
    from matplotlib.lines import Line2D
    from matplotlib.patches import Patch
    from matplotlib.ticker import FixedLocator, NullLocator

    layered = [r for r in readings if r.get("lcl") is not None]
    p_bottom, p_top = 1050.0, 185.0
    width = RECORD_MINUTES / 1440
    times, levels, thickness = cloud_density(layered)
    swatches = cloud_colours(np.array([(low + high) / 2 for low, high in zip(CLOUD_BANDS, CLOUD_BANDS[1:])]))
    ax.set_facecolor("#f7f7f9")
    ax.contourf(times, levels, np.ma.masked_invalid(thickness), levels=CLOUD_BANDS,
                colors=swatches, extend="max", antialiased=True, corner_mask=False, zorder=2)
    ax.set_ylim(np.log(p_bottom), np.log(p_top))  # surface at the bottom
    ticks = np.log(PRESSURE_TICKS)
    ax.yaxis.set_major_locator(FixedLocator(ticks))
    ax.yaxis.set_minor_locator(NullLocator())
    ax.set_yticklabels([str(p) for p in PRESSURE_TICKS])
    ax.grid(axis="y", color="#c9c9ce", lw=0.8, ls=(0, (4, 3)), zorder=3)

    # A tornado: a funnel from the lowered wall cloud down to the ground.
    for r in layered:
        if r.get("tornado"):
            middle = mdates.date2num(r["when"]) - width / 2
            top = np.log(min(r["lcl"], 870))
            ax.fill([middle - width * 0.9, middle + width * 0.9, middle + width * 0.12, middle - width * 0.12],
                    [top, top, np.log(p_bottom - 15), np.log(p_bottom - 15)],
                    color=TORNADO_COLOR, lw=0, zorder=3)

    lcl_times = [r["when"] - datetime.timedelta(minutes=RECORD_MINUTES / 2) for r in layered]
    ax.plot(lcl_times, np.log([r["lcl"] for r in layered]), color=LCL_COLOR, lw=1.6, ls=(0, (4, 2)), zorder=4)

    ax.text(0, 1.02, "hPa", transform=ax.transAxes, ha="right", va="bottom", fontsize=12, color="#444444")
    km = ax.secondary_yaxis("right")
    km.yaxis.set_major_locator(FixedLocator(ticks))
    km.yaxis.set_minor_locator(NullLocator())
    km.set_yticklabels([PRESSURE_KM[p] for p in PRESSURE_TICKS])
    km.tick_params(colors="#444444", labelsize=10, length=0)
    km.spines["right"].set_visible(False)
    ax.text(1, 1.02, "km", transform=ax.transAxes, ha="left", va="bottom", fontsize=12, color="#444444")

    handles = [Patch(facecolor="#dddddd", label="Thin cloud"),
               Patch(facecolor="#aaaaaa", label="Cloud"),
               Patch(facecolor="#666666", label="Dense cloud"),
               Line2D([], [], color=LCL_COLOR, lw=1.6, ls=(0, (4, 2)), label="LCL (cloud base)")]
    ax.legend(handles=handles, loc="upper left", bbox_to_anchor=(0.0, 1.17), ncol=4,
              frameon=False, fontsize=9, handlelength=1.2, handletextpad=0.4, columnspacing=1.2)


def plot(readings, out_path, hours=24, title=None):
    """Draw the chart into a PNG file."""
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig = plt.figure(figsize=(13, 10.4 if any(r.get("lcl") is not None for r in readings) else 7.2))
    draw(fig, readings, hours, title)
    fig.savefig(out_path, dpi=110, bbox_inches="tight")
    plt.close(fig)


def draw(fig, readings, hours=24, title=None):
    """Draw the chart into an (empty or reused) matplotlib figure."""
    import matplotlib
    import matplotlib.dates as mdates
    from matplotlib import transforms

    fig.clear()
    if not readings:
        raise ValueError("No readings to plot")
    end = readings[-1]["when"]
    if hours:
        begin = end - datetime.timedelta(hours=hours)
        readings = [r for r in readings if r["when"] >= begin]
    begin = readings[0]["when"]
    hour_data = hourly(readings)

    times = [r["when"] for r in readings]
    temperatures = [r["temperature"] for r in readings]
    dew_points = [dew_point(r["temperature"], r["humidity"]) for r in readings]

    has_layers = any(r.get("lcl") is not None for r in readings)
    if has_layers:
        ax_t, ax_c, ax_w = fig.subplots(
            3, 1, sharex=True, gridspec_kw={"height_ratios": [3.0, 2.2, 1.3], "hspace": 0.16})
    else:
        ax_t, ax_w = fig.subplots(2, 1, sharex=True, gridspec_kw={"height_ratios": [3.2, 1.3], "hspace": 0.12})
        ax_c = None
    fig.patch.set_facecolor("white")
    x_begin = (begin - datetime.timedelta(minutes=RECORD_MINUTES)).replace(minute=0)
    x_end = max((end + datetime.timedelta(minutes=59)).replace(minute=0), x_begin + datetime.timedelta(hours=1))

    # -- temperature panel ---------------------------------------------------
    t_low, t_high = nice_temperature_range(temperatures + dew_points)
    intervals = int((t_high - t_low) / 5)
    ax_t.set_ylim(t_low, t_high)
    ax_t.set_yticks(range(t_low, t_high + 1, 5))
    ax_t.grid(axis="y", color=GRID_COLOR, lw=1, zorder=1)
    if t_low < 0 < t_high:
        ax_t.axhline(0, color="black", lw=1, zorder=2)

    # Precipitation on a right-hand axis whose gridlines match the left one.
    ax_r = ax_t.twinx()
    max_rain = max((h["rain"] for h in hour_data), default=0.0)
    step = max(1, math.ceil(max_rain / intervals))
    ax_r.set_ylim(0, step * intervals)
    ax_r.set_yticks(range(0, step * intervals + 1, step))
    ax_r.bar(
        [h["start"] for h in hour_data],
        [h["rain"] for h in hour_data],
        width=datetime.timedelta(minutes=56),
        align="edge",
        color=RAIN_COLOR,
        zorder=3,
        label="Precipitation (mm/h)",
    )
    # Draw the lines above the bars: put the left axes in front.
    ax_t.set_zorder(ax_r.get_zorder() + 1)
    ax_t.patch.set_visible(False)

    # A dashed line at each midnight, across all panels.
    hour = x_begin
    while hour < x_end:
        if hour.hour == 0 and hour > x_begin:
            for ax in (ax_t, ax_w) + ((ax_c,) if ax_c is not None else ()):
                ax.axvline(hour, color="#777777", ls=(0, (3, 3)), lw=1, zorder=5)
        hour += datetime.timedelta(hours=1)

    ax_t.plot(times, temperatures, color=TEMPERATURE_COLOR, lw=2.2, zorder=4, label="Temperature")
    ax_t.plot(times, dew_points, color=DEW_POINT_COLOR, lw=2.2, ls=(0, (1, 1.6)), zorder=4, label="Dew point")

    ax_t.text(0, 1.02, "°C", transform=ax_t.transAxes, ha="right", va="bottom", fontsize=12, color="#444444")
    ax_r.text(1, 1.02, "mm", transform=ax_r.transAxes, ha="left", va="bottom", fontsize=12, color="#444444")
    handles = ax_t.get_legend_handles_labels()[0] + ax_r.get_legend_handles_labels()[0]
    ax_t.legend(handles=handles, loc="upper left", bbox_to_anchor=(0.0, 1.13), ncol=3, frameon=False, fontsize=10)
    if title:
        ax_t.set_title(title, loc="right", fontsize=11, color="#444444", pad=22)

    # -- cloud panel: types at their levels on a pressure axis -----------------
    if ax_c is not None:
        draw_cloud_panel(ax_c, readings)

    # -- wind panel ----------------------------------------------------------
    # Solid: the mean wind of each 10-minute record. Dotted: the gusts, the
    # highest wind sampled in each interval (older CSVs have none).
    speeds = [r["wind_speed"] for r in readings]
    gusts = [r.get("wind_gust") for r in readings]
    has_gusts = any(g is not None for g in gusts)
    top_speed = max(speeds + [g for g in gusts if g is not None])
    w_high = max(4, math.ceil(top_speed / 2) * 2)
    ax_w.set_ylim(0, w_high * 1.7)  # top part holds the arrows
    ax_w.set_yticks(range(0, w_high + 1, max(1, w_high // 2)))
    ax_w.grid(axis="y", color=GRID_COLOR, lw=1, zorder=1)
    ax_w.fill_between(times, speeds, color=WIND_COLOR, alpha=0.12, lw=0, zorder=2)
    ax_w.plot(times, speeds, color=WIND_COLOR, lw=1.6, zorder=3, label="Mean wind (m/s)")
    if has_gusts:
        ax_w.plot([t for t, g in zip(times, gusts) if g is not None], [g for g in gusts if g is not None],
                  color=WIND_COLOR, lw=1.4, ls=(0, (1, 1.8)), zorder=3, label="Gusts (m/s)")
        ax_w.legend(loc="lower left", bbox_to_anchor=(0.03, 0.98), ncol=2, frameon=False, fontsize=9,
                    handlelength=2.2, columnspacing=1.2)
    ax_w.text(0, 1.02, "m/s", transform=ax_w.transAxes, ha="right", va="bottom", fontsize=12, color="#444444")

    # Tornadoes: a red band over each record with one, labelled once.
    tornadoes = [r["when"] for r in readings if r.get("tornado")]
    for when in tornadoes:
        ax_w.axvspan(when - datetime.timedelta(minutes=RECORD_MINUTES), when, color=TORNADO_COLOR, alpha=0.22,
                     lw=0, zorder=1)
    if tornadoes:
        ax_w.text(tornadoes[0] - datetime.timedelta(minutes=RECORD_MINUTES * 1.5), top_speed, "Tornado",
                  ha="right", va="center", fontsize=10, fontweight="bold", color=TORNADO_COLOR, zorder=6)

    # Surface pressure on the right-hand axis (high in an anticyclone, deep
    # and falling as a storm's low passes).
    pressures = [r.get("pressure") for r in readings]
    if any(p is not None for p in pressures):
        ax_p = ax_w.twinx()
        points = [(t, p) for t, p in zip(times, pressures) if p is not None]
        values = [p for _, p in points]
        ax_p.plot([t for t, _ in points], values, color=PRESSURE_COLOR, lw=1.4, zorder=3)
        low, high = math.floor(min(values) / 5) * 5 - 5, math.ceil(max(values) / 5) * 5 + 5
        ax_p.set_ylim(low, low + (high - low) * 1.7)  # keep clear of the arrows
        ax_p.set_yticks(range(int(low), int(high) + 1, max(5, int((high - low) // 10 * 5) or 5)))
        ax_p.tick_params(colors=PRESSURE_COLOR, labelsize=10, length=0)
        for side in ("top", "left", "right"):
            ax_p.spines[side].set_visible(False)
        ax_p.spines["bottom"].set_visible(False)
        ax_p.text(1, 1.02, "hPa", transform=ax_p.transAxes, ha="left", va="bottom", fontsize=12,
                  color=PRESSURE_COLOR)

    # Hourly arrows pointing where the wind blows to (FMI style), with the
    # hourly mean speed underneath. (With many hours, every other one.)
    row = transforms.blended_transform_factory(ax_w.transData, ax_w.transAxes)
    ax_w.text(-0.006, 0.66, "m/s", transform=ax_w.transAxes, ha="right", va="center", fontsize=9,
              color=WIND_COLOR)
    arrow_points = 9
    arrow_every = 1 if len(hour_data) <= 50 else 2 if len(hour_data) <= 100 else 3
    for h in hour_data[::arrow_every]:
        # Custom transforms don't convert datetimes: use matplotlib date numbers.
        center = mdates.date2num(h["start"] + datetime.timedelta(minutes=30))
        to_direction = math.radians(h["wind_direction"] + 180)
        dx, dy = arrow_points * math.sin(to_direction), arrow_points * math.cos(to_direction)
        tail = transforms.offset_copy(row, fig=fig, x=-dx, y=-dy, units="points")
        ax_w.annotate(
            "",
            xy=(center, 0.85),
            xycoords=transforms.offset_copy(row, fig=fig, x=dx, y=dy, units="points"),
            xytext=(center, 0.85),
            textcoords=tail,
            arrowprops={"arrowstyle": "-|>", "color": WIND_COLOR, "lw": 1.8, "mutation_scale": 11},
            zorder=4,
        )
        ax_w.text(center, 0.66, f"{h['wind_speed']:.0f}", transform=row, ha="center", va="center",
                  fontsize=9, color=WIND_COLOR)

    # -- time axis -------------------------------------------------------------
    ax_w.set_xlim(x_begin, x_end)
    span_hours = (x_end - x_begin).total_seconds() / 3600
    tick_hours = 2 if span_hours <= 26 else 4 if span_hours <= 50 else 6 if span_hours <= 74 else 12
    ax_w.xaxis.set_major_locator(mdates.HourLocator(byhour=range(0, 24, tick_hours)))
    ax_w.xaxis.set_major_formatter(
        matplotlib.ticker.FuncFormatter(lambda value, _: f"{mdates.num2date(value).hour}:00")
    )
    day_of = {r["when"].date(): r["day"] for r in readings}
    for day_date, day_number in day_of.items():
        midnight = datetime.datetime.combine(day_date, datetime.time())
        if x_begin < midnight < x_end:
            ax_w.text(mdates.date2num(midnight), -0.2, f"Day {day_number}", transform=transforms.blended_transform_factory(
                ax_w.transData, ax_w.transAxes), ha="center", va="top", fontsize=10, fontweight="bold")

    for ax in (ax_t, ax_r, ax_w) + ((ax_c,) if ax_c is not None else ()):
        for side in ("top", "right", "left"):
            ax.spines[side].set_visible(False)
        ax.spines["bottom"].set_color("#aaaaaa")
        ax.tick_params(colors="#444444", labelsize=11, length=0)


# ---- demo: run the firmware in QEMU --------------------------------------------


def free_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def start_qemu(qemu, firmware, host_port, speed=None, scenario=None):
    """Run the firmware with its UDP service forwarded to `host_port`. `speed`
    and `scenario` set the firmware's switches (QEMU -fw_cfg
    opt/weather/speed and opt/weather/scenario)."""
    command = [
        qemu, "-M", "virt", "-cpu", "cortex-a53", "-m", "128M",
        "-nographic", "-semihosting", "-nic", "none",
        "-global", "virtio-mmio.force-legacy=false",
        "-netdev", f"user,id=net0,hostfwd=udp:127.0.0.1:{host_port}-:{SERVICE_PORT}",
        "-device", "virtio-net-device,netdev=net0",
        "-kernel", firmware,
    ]
    if speed:
        command += ["-fw_cfg", f"name=opt/weather/speed,string={speed}"]
    if scenario:
        command += ["-fw_cfg", f"name=opt/weather/scenario,string={scenario}"]
    return subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)


# ---- record: collect live measurements and keep the chart up to date ---------


LOG_CAPACITY = 256  # records the firmware keeps (weather_log::CAPACITY)


def safe_interval(speed):
    """Real seconds between imports that keep well inside the firmware's log:
    a quarter of the time it takes to fill it, at most 2 s."""
    records_per_second = speed / (RECORD_MINUTES * 60)  # one per 600 simulated s
    if records_per_second <= 0:
        return 2.0
    return max(0.2, min(2.0, LOG_CAPACITY / records_per_second / 4))


def record(client, csv_path, out_path, hours, interval, plot_hours, live, title):
    """Import new records every `interval` real seconds (None: chosen from the
    speed) and redraw the chart, until `hours` simulated hours have been
    recorded (0 = until Ctrl-C or the live window is closed)."""
    figure = None
    if live:
        import matplotlib.pyplot as plt

        plt.ion()
        figure = plt.figure(figsize=(13, 7.2))
    speed = client.command("speed").split()[-1]
    if interval is None:
        interval = safe_interval(int(speed))
    print(f"Recording at {speed}x into {csv_path}, importing every {interval:.2f} s"
          + (f" for {hours} simulated hours" if hours else " (Ctrl-C stops)"))
    if speed == "1":
        print("Real time: the station logs a record every 10 minutes.")
    recorded = 0
    try:
        while True:
            added = fetch(client, csv_path)
            recorded += added
            readings = load(csv_path)
            if added and readings:
                last = readings[-1]
                print(f"  day {last['day']} {last['when']:%H:%M}  {last['temperature']:5.1f} C  "
                      f"{last['wind_speed']:4.1f} m/s  rain {last['rain']:.1f} mm   ({len(readings)} records)")
                if live:
                    draw(figure, readings, plot_hours, title)
                    figure.canvas.draw_idle()
                else:
                    plot(readings, out_path, plot_hours, title)
            if hours and recorded >= hours * 60 // RECORD_MINUTES:
                break
            if live:
                import matplotlib.pyplot as plt

                if not plt.fignum_exists(figure.number):
                    break  # window closed
                plt.pause(interval)
            else:
                time.sleep(interval)
    except KeyboardInterrupt:
        print("Stopped.")
    readings = load(csv_path)
    if readings:
        plot(readings, out_path, plot_hours, title)
        print(f"Wrote {out_path} ({len(readings)} records)")


# ---- command line --------------------------------------------------------------


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p_fetch = sub.add_parser("fetch", help="import logged readings from the firmware into a CSV file")
    p_fetch.add_argument("--host", default="127.0.0.1")
    p_fetch.add_argument("--port", type=int, default=5555, help="host UDP port forwarded to the firmware")
    p_fetch.add_argument("--out", default="weather.csv")
    p_fetch.add_argument("--advance", type=int, metavar="MINUTES", help="fast-forward the simulation first")

    p_plot = sub.add_parser("plot", help="plot a CSV file in the FMI chart style")
    p_plot.add_argument("csv")
    p_plot.add_argument("--out", default="weather.png")
    p_plot.add_argument("--hours", type=int, default=24, help="plot the last N hours (0 = all)")

    p_rec = sub.add_parser("record", help="collect live measurements and keep the chart up to date")
    p_rec.add_argument("--speed", type=int, help="simulation speed: 1 = real time (default), up to 72000")
    p_rec.add_argument("--hours", type=int, default=0, help="simulated hours to record (0 = until Ctrl-C)")
    p_rec.add_argument("--interval", type=float,
                       help="real seconds between imports (default: from the speed, fast enough "
                            "that the firmware's log never wraps between imports)")
    p_rec.add_argument("--port", type=int, help="use an already running firmware on this host UDP port")
    p_rec.add_argument("--firmware", default="build/firmware.elf")
    p_rec.add_argument("--qemu", default="qemu-system-aarch64")
    p_rec.add_argument("--csv", default="weather.csv")
    p_rec.add_argument("--out", default="weather.png")
    p_rec.add_argument("--plot-hours", type=int, default=48, help="chart the last N hours (0 = all)")
    p_rec.add_argument("--live", action="store_true", help="show a live chart window as well")
    p_rec.add_argument("--scenario", choices=SCENARIOS, help="weather scenario (default: the firmware's default)")

    p_demo = sub.add_parser("demo", help="start QEMU, fast-forward, import and plot")
    p_demo.add_argument("--firmware", default="build/firmware.elf")
    p_demo.add_argument("--qemu", default="qemu-system-aarch64")
    p_demo.add_argument("--minutes", type=int, default=24 * 60, help="simulated minutes to fast-forward")
    p_demo.add_argument("--csv", default="weather.csv")
    p_demo.add_argument("--out", default="weather.png")
    p_demo.add_argument("--hours", type=int, default=24)
    p_demo.add_argument("--scenario", choices=SCENARIOS, help="weather scenario (default: the firmware's default)")

    args = parser.parse_args(argv)

    if args.command == "fetch":
        added = fetch(WeatherClient(args.host, args.port), args.out, args.advance)
        print(f"Imported {added} new readings into {args.out}")
    elif args.command == "plot":
        plot(load(args.csv), args.out, args.hours, title=os.path.basename(args.csv))
        print(f"Wrote {args.out}")
    elif args.command == "record":
        # Stop cleanly on `kill` (SIGTERM) as well as Ctrl-C: write the final
        # chart and shut down QEMU.
        signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt))
        qemu = None
        if args.port:
            client = WeatherClient(port=args.port)
            client.wait_until_ready()
            if args.speed:
                print(client.command(f"speed {args.speed}").strip())
            if args.scenario:
                print(client.command(f"scenario {args.scenario}").strip())
        else:
            if os.path.exists(args.csv):
                os.remove(args.csv)  # a fresh QEMU starts a fresh log
            port = free_udp_port()
            qemu = start_qemu(args.qemu, args.firmware, port, args.speed, args.scenario)
            client = WeatherClient(port=port)
            client.wait_until_ready()
        try:
            scenario = client.command("scenario").split()[1].rstrip(":")
            record(client, args.csv, args.out, args.hours, args.interval, args.plot_hours, args.live,
                   title=f"simulated weather station: {scenario}, {client.command('speed').split()[-1]}x")
        finally:
            if qemu:
                qemu.kill()
                qemu.wait()
    elif args.command == "demo":
        if os.path.exists(args.csv):
            os.remove(args.csv)  # a fresh QEMU starts a fresh log
        port = free_udp_port()
        qemu = start_qemu(args.qemu, args.firmware, port, scenario=args.scenario)
        try:
            client = WeatherClient(port=port)
            client.wait_until_ready()
            scenario = client.command("scenario").split()[1].rstrip(":")
            added = fetch(client, args.csv, args.minutes)
            print(f"Imported {added} readings into {args.csv}")
        finally:
            qemu.kill()
            qemu.wait()
        plot(load(args.csv), args.out, args.hours, title=f"simulated weather station: {scenario}")
        print(f"Wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
