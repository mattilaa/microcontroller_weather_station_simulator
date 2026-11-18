"""Summary of a day of weather records, for the scenario tests."""

import math


def _direction_mean(degrees):
    """Mean of directions on the circle, in degrees."""
    u = sum(math.sin(math.radians(d)) for d in degrees)
    v = sum(math.cos(math.radians(d)) for d in degrees)
    return math.degrees(math.atan2(u, v)) % 360


def summarise(log):
    def values(key):
        return [r[key] for r in log]

    hourly = {}
    for r in log:
        key = (r["day"], r["time"][:2])
        hourly[key] = hourly.get(key, 0.0) + r["rain"]
    wind = values("wind_speed")
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
    }
