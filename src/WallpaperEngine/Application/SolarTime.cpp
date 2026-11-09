#include "SolarTime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace WallpaperEngine::Application::SolarTime {
namespace {
    constexpr double PI = 3.14159265358979323846;

    double toRadians (const double degrees) { return degrees * PI / 180.0; }
    double toDegrees (const double radians) { return radians * 180.0 / PI; }

    /**
     * Parses the whole string as a double, std::nullopt if there's anything else in it
     */
    std::optional<double> parseNumber (const std::string& text) {
	if (text.empty ()) {
	    return std::nullopt;
	}

	char* end = nullptr;
	const double value = std::strtod (text.c_str (), &end);

	if (end != text.c_str () + text.size () || !std::isfinite (value)) {
	    return std::nullopt;
	}

	return value;
    }

    /**
     * UTC minutes after midnight (of the date the julian day starts at) at which the event happens,
     * based on NOAA's solar calculator: https://gml.noaa.gov/grad/solcalc/calcdetails.html
     */
    std::optional<double>
    eventMinutes (const Event event, const Location& location, const double julianDay, const double minutes) {
	const double t = (julianDay + minutes / 1440.0 - 2451545.0) / 36525.0;

	const double meanLongitude = std::fmod (280.46646 + t * (36000.76983 + t * 0.0003032), 360.0);
	const double meanAnomaly = 357.52911 + t * (35999.05029 - 0.0001537 * t);
	const double eccentricity = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
	const double equationOfCenter = std::sin (toRadians (meanAnomaly)) * (1.914602 - t * (0.004817 + 0.000014 * t))
	    + std::sin (toRadians (2.0 * meanAnomaly)) * (0.019993 - 0.000101 * t)
	    + std::sin (toRadians (3.0 * meanAnomaly)) * 0.000289;
	const double trueLongitude = meanLongitude + equationOfCenter;
	const double omega = 125.04 - 1934.136 * t;
	const double apparentLongitude = trueLongitude - 0.00569 - 0.00478 * std::sin (toRadians (omega));
	const double meanObliquity
	    = 23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
	const double obliquity = meanObliquity + 0.00256 * std::cos (toRadians (omega));
	const double declination
	    = toDegrees (std::asin (std::sin (toRadians (obliquity)) * std::sin (toRadians (apparentLongitude))));

	const double y = std::pow (std::tan (toRadians (obliquity / 2.0)), 2.0);
	const double l0 = toRadians (meanLongitude);
	const double m = toRadians (meanAnomaly);
	const double equationOfTime = 4.0
	    * toDegrees (y * std::sin (2.0 * l0) - 2.0 * eccentricity * std::sin (m)
			 + 4.0 * eccentricity * y * std::sin (m) * std::cos (2.0 * l0)
			 - 0.5 * y * y * std::sin (4.0 * l0) - 1.25 * eccentricity * eccentricity * std::sin (2.0 * m));

	const double latitude = toRadians (location.latitude);
	const double decl = toRadians (declination);
	const double cosHourAngle = std::cos (toRadians (90.833)) / (std::cos (latitude) * std::cos (decl))
	    - std::tan (latitude) * std::tan (decl);

	// the sun stays above (midnight sun) or below (polar night) the horizon the whole day
	if (cosHourAngle < -1.0 || cosHourAngle > 1.0) {
	    return std::nullopt;
	}

	const double hourAngle = toDegrees (std::acos (cosHourAngle));
	const double solarNoon = 720.0 - 4.0 * location.longitude - equationOfTime;

	return event == Event::Sunrise ? solarNoon - 4.0 * hourAngle : solarNoon + 4.0 * hourAngle;
    }
} // namespace

std::optional<Expression> parseExpression (const std::string& text) {
    std::string lower = text;
    std::transform (lower.begin (), lower.end (), lower.begin (), [] (const unsigned char c) {
	return static_cast<char> (std::tolower (c));
    });
    lower.erase (
	std::remove_if (lower.begin (), lower.end (), [] (const unsigned char c) { return std::isspace (c); }),
	lower.end ()
    );

    Expression result {};
    std::string rest;

    if (lower.rfind ("sunrise", 0) == 0) {
	result.event = Event::Sunrise;
	rest = lower.substr (7);
    } else if (lower.rfind ("sunset", 0) == 0) {
	result.event = Event::Sunset;
	rest = lower.substr (6);
    } else {
	return std::nullopt;
    }

    result.offsetHours = 0.0;

    if (rest.empty ()) {
	return result;
    }

    if (rest[0] != '+' && rest[0] != '-') {
	return std::nullopt;
    }

    // minutes unless stated otherwise
    double unit = 1.0 / 60.0;

    if (rest.ends_with ("min")) {
	rest.resize (rest.size () - 3);
    } else if (rest.ends_with ('m')) {
	rest.resize (rest.size () - 1);
    } else if (rest.ends_with ('h')) {
	rest.resize (rest.size () - 1);
	unit = 1.0;
    }

    const auto amount = parseNumber (rest);

    if (!amount.has_value () || std::abs (*amount * unit) >= 24.0) {
	return std::nullopt;
    }

    result.offsetHours = *amount * unit;
    return result;
}

std::optional<Location> parseLocation (const std::string& text) {
    const auto comma = text.find (',');

    if (comma == std::string::npos) {
	return std::nullopt;
    }

    const auto trim = [] (std::string value) {
	const auto first = value.find_first_not_of (" \t");
	const auto last = value.find_last_not_of (" \t");
	return first == std::string::npos ? std::string () : value.substr (first, last - first + 1);
    };

    const auto latitude = parseNumber (trim (text.substr (0, comma)));
    const auto longitude = parseNumber (trim (text.substr (comma + 1)));

    if (!latitude.has_value () || !longitude.has_value () || std::abs (*latitude) > 90.0
	|| std::abs (*longitude) > 180.0) {
	return std::nullopt;
    }

    return Location { .latitude = *latitude, .longitude = *longitude };
}

std::optional<std::time_t>
eventTime (const Event event, const Location& location, const int year, const int month, const int day) {
    std::tm date {};
    date.tm_year = year - 1900;
    date.tm_mon = month - 1;
    date.tm_mday = day;

    const std::time_t midnight = timegm (&date);
    const double julianDay = static_cast<double> (midnight) / 86400.0 + 2440587.5;

    // first estimate with the sun's position at the location's noon, then refine it at the estimated time
    auto minutes = eventMinutes (event, location, julianDay, 720.0 - 4.0 * location.longitude);

    for (int i = 0; i < 2 && minutes.has_value (); i++) {
	minutes = eventMinutes (event, location, julianDay, *minutes);
    }

    if (!minutes.has_value ()) {
	return std::nullopt;
    }

    return midnight + static_cast<std::time_t> (std::lround (*minutes * 60.0));
}

std::optional<double> localHours (const Expression& expression, const Location& location, const std::time_t now) {
    std::tm today {};
    localtime_r (&now, &today);

    const auto event = eventTime (expression.event, location, today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);

    if (!event.has_value ()) {
	return std::nullopt;
    }

    const std::time_t shifted = *event + static_cast<std::time_t> (std::lround (expression.offsetHours * 3600.0));
    std::tm local {};
    localtime_r (&shifted, &local);

    return local.tm_hour + local.tm_min / 60.0 + local.tm_sec / 3600.0;
}

std::string formatHours (const double hours) {
    const int totalMinutes = static_cast<int> (std::lround (hours * 60.0)) % (24 * 60);
    char buffer[8];
    std::snprintf (buffer, sizeof (buffer), "%02d:%02d", totalMinutes / 60, totalMinutes % 60);
    return buffer;
}
} // namespace WallpaperEngine::Application::SolarTime
