#pragma once

#include <ctime>
#include <optional>
#include <string>

/**
 * Sunrise and sunset calculations used to drive user properties from the position of the sun,
 * e.g. `--location 43.46,-80.52 --set-property daystarttime=sunrise --set-property nightstarttime=sunset`
 *
 * The times follow NOAA's solar calculator equations (accurate to about a minute for latitudes
 * between +/- 72 degrees) and use the standard -0.833 degrees altitude for the sun's upper limb.
 */
namespace WallpaperEngine::Application::SolarTime {
enum class Event {
    Sunrise,
    Sunset,
};

/**
 * A property value bound to a solar event, written as `sunrise`/`sunset` with an optional offset:
 * `sunrise+30m`, `sunset-1h`, `sunset+1.5h`, `sunrise-45` (minutes when there's no unit)
 */
struct Expression {
    Event event;
    /** Offset added to the event, in hours */
    double offsetHours;
};

struct Location {
    double latitude;
    double longitude;
};

/**
 * @return The parsed expression, std::nullopt if the text is not a solar expression
 */
[[nodiscard]] std::optional<Expression> parseExpression (const std::string& text);

/**
 * Parses a `latitude,longitude` pair in decimal degrees (north and east are positive)
 *
 * @return The location, std::nullopt if the text is malformed or out of range
 */
[[nodiscard]] std::optional<Location> parseLocation (const std::string& text);

/**
 * Calculates when the given event happens on the given calendar date, as a day at the location
 * (from the solar midnight before that date's noon to the one after)
 *
 * @return The event's unix timestamp, std::nullopt if the sun doesn't rise/set that day (polar day or night)
 */
[[nodiscard]] std::optional<std::time_t>
eventTime (Event event, const Location& location, int year, int month, int day);

/**
 * Calculates the local time of day the expression resolves to on the local date of `now`
 *
 * @return Fractional hours in the [0, 24) range on the local clock (7.5 = 07:30),
 *         std::nullopt if the sun doesn't rise/set that day
 */
[[nodiscard]] std::optional<double>
localHours (const Expression& expression, const Location& location, std::time_t now);

/**
 * @return The given fractional hours formatted as HH:MM
 */
[[nodiscard]] std::string formatHours (double hours);
} // namespace WallpaperEngine::Application::SolarTime
