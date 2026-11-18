#!/usr/bin/env python3
"""Import weather data from the firmware and plot it.

The firmware logs every reading (one per 10 simulated minutes) and serves the
log over its UDP service (port 5000 in the guest; the CMake `run` target
forwards host port 5555 to it).

    # 1. Start the firmware:      cmake --build build --target run
    # 2. Import its sensor data:  python tools/weather_tool.py fetch --out weather.csv
    # 3. Plot it:                 python tools/weather_tool.py plot weather.csv --out weather.png

    # Or all in one, with its own QEMU and a fast-forwarded simulated day:
    python tools/weather_tool.py demo --firmware build/firmware.elf --minutes 1440

`fetch` appends only readings that aren't in the CSV yet, so it can be run
repeatedly. `--advance MINUTES` fast-forwards the simulation first.

The chart follows the Finnish Meteorological Institute's daily forecast chart:
temperature (red) and dew point (green, dotted) on the left axis, hourly
precipitation bars on the right axis, alternating hour bands, a dashed line at
midnight, and a wind panel underneath with hourly direction arrows and speeds.
"""

import argparse
import csv
import datetime
import math
import os
import socket
import subprocess
import sys
import time

COLUMNS = ["seq", "day", "time", "temperature", "humidity", "wind_speed", "wind_direction", "rain"]
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
    new_file = last is None
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


def hourly(readings):
    """Group readings into clock hours: precipitation sum, mean wind speed and
    vector-averaged wind direction."""
    hours = {}
    for r in readings:
        key = r["when"].replace(minute=0)
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
WIND_COLOR = "#2f4f6f"
BAND_COLOR = "#f4f4f4"
GRID_COLOR = "#dddddd"


def nice_temperature_range(values, intervals_min=4):
    """Axis limits on multiples of 5 degC with room for the rain bars at the
    bottom, like the FMI chart (at least `intervals_min` gridlines apart)."""
    low = math.floor((min(values) - 1) / 5) * 5
    high = math.ceil((max(values) + 1) / 5) * 5
    while (high - low) / 5 < intervals_min:
        low -= 5
    return low, high


def plot(readings, out_path, hours=24, title=None):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.dates as mdates
    import matplotlib.pyplot as plt
    from matplotlib import transforms

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

    fig, (ax_t, ax_w) = plt.subplots(
        2, 1, figsize=(13, 7.2), sharex=True, gridspec_kw={"height_ratios": [3.2, 1.3], "hspace": 0.12}
    )
    fig.patch.set_facecolor("white")
    x_begin = begin.replace(minute=0)
    x_end = (end + datetime.timedelta(minutes=59)).replace(minute=0)

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

    # Alternating hour bands and a dashed line at each midnight. In the top
    # panel they go on the back (rain) axes, or they would cover the bars.
    hour = x_begin
    while hour < x_end:
        if hour.hour % 2 == 1:
            for ax in (ax_r, ax_w):
                ax.axvspan(hour, hour + datetime.timedelta(hours=1), color=BAND_COLOR, zorder=0, lw=0)
        if hour.hour == 0 and hour > x_begin:
            for ax in (ax_t, ax_w):
                ax.axvline(hour, color="#777777", ls=(0, (3, 3)), lw=1, zorder=1)
        hour += datetime.timedelta(hours=1)

    ax_t.plot(times, temperatures, color=TEMPERATURE_COLOR, lw=2.2, zorder=4, label="Temperature")
    ax_t.plot(times, dew_points, color=DEW_POINT_COLOR, lw=2.2, ls=(0, (1, 1.6)), zorder=4, label="Dew point")

    ax_t.text(0, 1.02, "°C", transform=ax_t.transAxes, ha="right", va="bottom", fontsize=12, color="#444444")
    ax_r.text(1, 1.02, "mm", transform=ax_r.transAxes, ha="left", va="bottom", fontsize=12, color="#444444")
    handles = ax_t.get_legend_handles_labels()[0] + ax_r.get_legend_handles_labels()[0]
    ax_t.legend(handles=handles, loc="upper left", bbox_to_anchor=(0.0, 1.13), ncol=3, frameon=False, fontsize=10)
    if title:
        ax_t.set_title(title, loc="right", fontsize=11, color="#444444", pad=22)

    # -- wind panel ----------------------------------------------------------
    speeds = [r["wind_speed"] for r in readings]
    w_high = max(4, math.ceil(max(speeds) / 2) * 2)
    ax_w.set_ylim(0, w_high * 1.7)  # top part holds the arrows
    ax_w.set_yticks(range(0, w_high + 1, max(1, w_high // 2)))
    ax_w.grid(axis="y", color=GRID_COLOR, lw=1, zorder=1)
    ax_w.fill_between(times, speeds, color=WIND_COLOR, alpha=0.12, lw=0, zorder=2)
    ax_w.plot(times, speeds, color=WIND_COLOR, lw=1.6, zorder=3, label="Wind speed")
    ax_w.text(0, 1.02, "m/s", transform=ax_w.transAxes, ha="right", va="bottom", fontsize=12, color="#444444")

    # Hourly arrows pointing where the wind blows to (FMI style), with the
    # hourly mean speed underneath.
    row = transforms.blended_transform_factory(ax_w.transData, ax_w.transAxes)
    arrow_points = 9
    for h in hour_data:
        # Custom transforms don't convert datetimes: use matplotlib date numbers.
        center = mdates.date2num(h["start"] + datetime.timedelta(minutes=30))
        to_direction = math.radians(h["wind_direction"] + 180)
        dx, dy = arrow_points * math.sin(to_direction), arrow_points * math.cos(to_direction)
        tail = transforms.offset_copy(row, fig=fig, x=-dx, y=-dy, units="points")
        ax_w.annotate(
            "",
            xy=(center, 0.80),
            xycoords=transforms.offset_copy(row, fig=fig, x=dx, y=dy, units="points"),
            xytext=(center, 0.80),
            textcoords=tail,
            arrowprops={"arrowstyle": "-|>", "color": WIND_COLOR, "lw": 1.8, "mutation_scale": 11},
            zorder=4,
        )
        ax_w.text(center, 0.55, f"{h['wind_speed']:.0f}", transform=row, ha="center", va="center",
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

    for ax in (ax_t, ax_r, ax_w):
        for side in ("top", "right", "left"):
            ax.spines[side].set_visible(False)
        ax.spines["bottom"].set_color("#aaaaaa")
        ax.tick_params(colors="#444444", labelsize=11, length=0)

    fig.savefig(out_path, dpi=110, bbox_inches="tight")
    plt.close(fig)


# ---- demo: run the firmware in QEMU --------------------------------------------


def free_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def start_qemu(qemu, firmware, host_port):
    command = [
        qemu, "-M", "virt", "-cpu", "cortex-a53", "-m", "128M",
        "-nographic", "-semihosting", "-nic", "none",
        "-global", "virtio-mmio.force-legacy=false",
        "-netdev", f"user,id=net0,hostfwd=udp:127.0.0.1:{host_port}-:{SERVICE_PORT}",
        "-device", "virtio-net-device,netdev=net0",
        "-kernel", firmware,
    ]
    return subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)


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

    p_demo = sub.add_parser("demo", help="start QEMU, fast-forward, import and plot")
    p_demo.add_argument("--firmware", default="build/firmware.elf")
    p_demo.add_argument("--qemu", default="qemu-system-aarch64")
    p_demo.add_argument("--minutes", type=int, default=24 * 60, help="simulated minutes to fast-forward")
    p_demo.add_argument("--csv", default="weather.csv")
    p_demo.add_argument("--out", default="weather.png")
    p_demo.add_argument("--hours", type=int, default=24)

    args = parser.parse_args(argv)

    if args.command == "fetch":
        added = fetch(WeatherClient(args.host, args.port), args.out, args.advance)
        print(f"Imported {added} new readings into {args.out}")
    elif args.command == "plot":
        plot(load(args.csv), args.out, args.hours, title=os.path.basename(args.csv))
        print(f"Wrote {args.out}")
    elif args.command == "demo":
        if os.path.exists(args.csv):
            os.remove(args.csv)  # a fresh QEMU starts a fresh log
        port = free_udp_port()
        qemu = start_qemu(args.qemu, args.firmware, port)
        try:
            client = WeatherClient(port=port)
            client.wait_until_ready()
            added = fetch(client, args.csv, args.minutes)
            print(f"Imported {added} readings into {args.csv}")
        finally:
            qemu.kill()
            qemu.wait()
        plot(load(args.csv), args.out, args.hours, title="simulated weather station")
        print(f"Wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
