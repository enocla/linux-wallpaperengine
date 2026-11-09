#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Application/SolarTime.h"

#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>

namespace SolarTime = WallpaperEngine::Application::SolarTime;
using SolarTime::Event;
using SolarTime::Location;

namespace {
constexpr Location London { .latitude = 51.5074, .longitude = -0.1278 };
constexpr Location Sydney { .latitude = -33.8688, .longitude = 151.2093 };
constexpr Location Tokyo { .latitude = 35.6762, .longitude = 139.6503 };
constexpr Location Tromso { .latitude = 69.6496, .longitude = 18.9560 };

/** Margin of error accepted against published times, in hours */
constexpr double Margin = 3.0 / 60.0;

std::time_t utc (int year, int month, int day, int hour, int minute) {
    std::tm date {};
    date.tm_year = year - 1900;
    date.tm_mon = month - 1;
    date.tm_mday = day;
    date.tm_hour = hour;
    date.tm_min = minute;
    return timegm (&date);
}

/**
 * Runs the given function with a POSIX TZ (no tzdata required), restoring the previous one afterwards
 */
template <typename F> void withTimezone (const char* timezone, F&& function) {
    const char* previous = std::getenv ("TZ");
    const std::optional<std::string> saved = previous ? std::optional<std::string> (previous) : std::nullopt;

    setenv ("TZ", timezone, 1);
    tzset ();
    function ();

    if (saved.has_value ()) {
	setenv ("TZ", saved->c_str (), 1);
    } else {
	unsetenv ("TZ");
    }

    tzset ();
}
} // namespace

TEST_CASE ("Solar expressions are parsed with their offsets") {
    const auto sunrise = SolarTime::parseExpression ("sunrise");
    REQUIRE (sunrise.has_value ());
    CHECK (sunrise->event == Event::Sunrise);
    CHECK (sunrise->offsetHours == Catch::Approx (0.0));

    const auto sunset = SolarTime::parseExpression ("Sunset-30m");
    REQUIRE (sunset.has_value ());
    CHECK (sunset->event == Event::Sunset);
    CHECK (sunset->offsetHours == Catch::Approx (-0.5));

    CHECK (SolarTime::parseExpression ("sunrise+1.5h")->offsetHours == Catch::Approx (1.5));
    CHECK (SolarTime::parseExpression ("sunrise-45")->offsetHours == Catch::Approx (-0.75));
    CHECK (SolarTime::parseExpression ("sunset + 90min")->offsetHours == Catch::Approx (1.5));

    CHECK_FALSE (SolarTime::parseExpression ("7").has_value ());
    CHECK_FALSE (SolarTime::parseExpression ("sunrise30m").has_value ());
    CHECK_FALSE (SolarTime::parseExpression ("sunrise+abc").has_value ());
    CHECK_FALSE (SolarTime::parseExpression ("sunset+25h").has_value ());
    CHECK_FALSE (SolarTime::parseExpression ("sunsets").has_value ());
}

TEST_CASE ("Locations are parsed and validated") {
    const auto location = SolarTime::parseLocation ("43.46, -80.52");
    REQUIRE (location.has_value ());
    CHECK (location->latitude == Catch::Approx (43.46));
    CHECK (location->longitude == Catch::Approx (-80.52));

    CHECK_FALSE (SolarTime::parseLocation ("43.46").has_value ());
    CHECK_FALSE (SolarTime::parseLocation ("91,0").has_value ());
    CHECK_FALSE (SolarTime::parseLocation ("0,181").has_value ());
    CHECK_FALSE (SolarTime::parseLocation ("north,east").has_value ());
}

TEST_CASE ("Sunrise and sunset match published times") {
    // London, summer solstice 2024: 04:43 and 21:21 BST
    const auto londonSunrise = SolarTime::eventTime (Event::Sunrise, London, 2024, 6, 21);
    const auto londonSunset = SolarTime::eventTime (Event::Sunset, London, 2024, 6, 21);
    REQUIRE (londonSunrise.has_value ());
    REQUIRE (londonSunset.has_value ());
    CHECK ((*londonSunrise - utc (2024, 6, 21, 3, 43)) / 3600.0 == Catch::Approx (0.0).margin (Margin));
    CHECK ((*londonSunset - utc (2024, 6, 21, 20, 21)) / 3600.0 == Catch::Approx (0.0).margin (Margin));
}

TEST_CASE ("The sun does not rise or set during polar day and night") {
    CHECK_FALSE (SolarTime::eventTime (Event::Sunrise, Tromso, 2024, 6, 21).has_value ());
    CHECK_FALSE (SolarTime::eventTime (Event::Sunset, Tromso, 2024, 6, 21).has_value ());
    CHECK_FALSE (SolarTime::eventTime (Event::Sunrise, Tromso, 2024, 12, 21).has_value ());
    CHECK_FALSE (SolarTime::eventTime (Event::Sunset, Tromso, 2024, 12, 21).has_value ());
}

TEST_CASE ("Solar expressions resolve to the local clock") {
    const auto sunrise = *SolarTime::parseExpression ("sunrise");
    const auto sunset = *SolarTime::parseExpression ("sunset");

    // Tokyo, summer solstice 2024: 04:25 and 19:00 JST
    withTimezone ("JST-9", [&] {
	const std::time_t now = utc (2024, 6, 21, 3, 0);
	CHECK (*SolarTime::localHours (sunrise, Tokyo, now) == Catch::Approx (4.0 + 25.0 / 60.0).margin (Margin));
	CHECK (*SolarTime::localHours (sunset, Tokyo, now) == Catch::Approx (19.0).margin (Margin));

	const auto later = *SolarTime::parseExpression ("sunrise+30m");
	CHECK (*SolarTime::localHours (later, Tokyo, now) == Catch::Approx (4.0 + 55.0 / 60.0).margin (Margin));
    });

    // Sydney, 21 December 2024: 05:41 and 20:05 AEDT, sunrise happens on the previous UTC day
    withTimezone ("AEDT-11", [&] {
	const std::time_t now = utc (2024, 12, 20, 23, 0);
	CHECK (*SolarTime::localHours (sunrise, Sydney, now) == Catch::Approx (5.0 + 41.0 / 60.0).margin (Margin));
	CHECK (*SolarTime::localHours (sunset, Sydney, now) == Catch::Approx (20.0 + 5.0 / 60.0).margin (Margin));
    });

    CHECK (SolarTime::formatHours (7.5) == "07:30");
    CHECK (SolarTime::formatHours (23.999) == "00:00");
}
