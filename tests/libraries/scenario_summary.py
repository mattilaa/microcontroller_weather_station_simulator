"""Summary of a day of weather records, for the scenario tests."""

import math


def _direction_mean(degrees):
    """Mean of directions on the circle, in degrees."""
    u = sum(math.sin(math.radians(d)) for d in degrees)
    v = sum(math.cos(math.radians(d)) for d in degrees)
    return math.degrees(math.atan2(u, v)) % 360


def _mean(values):
    return sum(values) / len(values) if values else 0.0


def _first(log, condition):
    """Index of the first record meeting the condition, or None."""
    return next((i for i, r in enumerate(log) if condition(r)), None)


def summarise(log):
    def values(key):
        return [r[key] for r in log]

    hourly = {}
    for r in log:
        key = (r["day"], r["time"][:2])
        hourly[key] = hourly.get(key, 0.0) + r["rain"]
    wind = values("wind_speed")
    first_rain = _first(log, lambda r: r["rain"] > 0)
    first_cb = _first(log, lambda r: r["cumulonimbus"] >= 10)
    storm = _first(log, lambda r: r["wind_speed"] >= 15)
    storm_end = storm
    while storm is not None and storm_end + 1 < len(log) and log[storm_end + 1]["wind_speed"] >= 15:
        storm_end += 1
    tornadoes = [i for i, r in enumerate(log) if r.get("tornado")]
    first = _direction_mean([r["wind_direction"] for r in log[:6]])
    last = _direction_mean([r["wind_direction"] for r in log[-6:]])
    return {
        "records": len(log),
        "temperature_min": min(values("temperature")),
        "temperature_max": max(values("temperature")),
        "humidity_min": min(values("humidity")),
        "wind_mean": sum(wind) / len(wind),
        "wind_max": max(wind),
        "gust_max": max(values("wind_gust")),
        "rain": round(sum(values("rain")), 2),
        "rain_max_hour": round(max(hourly.values()), 2),
        "pressure_min": min(values("pressure")),
        "pressure_mean": sum(values("pressure")) / len(log),
        "cumulus_max": max(values("cumulus")),
        "cumulonimbus_max": max(values("cumulonimbus")),
        "nimbostratus_max": max(values("nimbostratus")),
        # Clockwise turn from the first hour to the last, 0..360.
        "veering": (last - first) % 360,
        # What comes first: hours (records are 10 minutes) to the first
        # rain and the first cumulonimbus, and the cirrus in the hour
        # before each.
        "first_rain_hours": None if first_rain is None else first_rain / 6,
        "cirrus_before_rain": _mean([r["cirrus"] for r in log[max(first_rain or 0, 6) - 6:first_rain or 0]]),
        "wind_before_rain": _mean([r["wind_speed"] for r in log[:first_rain or 0]]),
        "first_cumulonimbus_hours": None if first_cb is None else first_cb / 6,
        "cirrus_before_cumulonimbus": _mean([r["cirrus"] for r in log[max(first_cb or 0, 6) - 6:first_cb or 0]]),
        "cirrus_first_hour": _mean([r["cirrus"] for r in log[:6]]),
        # A convective storm event: the first stretch of records with a
        # mean wind of at least 15 m/s.
        "storm_minutes": 0 if storm is None else (storm_end - storm + 1) * 10,
        "storm_wind_min": min(wind[storm:storm_end + 1]) if storm is not None else 0.0,
        "front_wind": _mean(wind[storm:storm + 6]) if storm is not None else 0.0,
        "late_wind": _mean(wind[storm + 24:storm + 36]) if storm is not None else 0.0,
        "storm_start": storm,
        "storm_end": storm_end,
        "tornado_records": len(tornadoes),
        "first_tornado": tornadoes[0] if tornadoes else None,
        "tornado_gust": max((log[i]["wind_gust"] for i in tornadoes), default=0.0),
    }
