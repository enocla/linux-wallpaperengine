#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Parsers/DynamicValueParser.h"
#include "WallpaperEngine/Data/Parsers/UserSettingParser.h"

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Model::DynamicValue;
using WallpaperEngine::Data::Model::Properties;
using WallpaperEngine::Data::Model::PropertySlider;
using WallpaperEngine::Data::Parsers::DynamicValueParser;
using WallpaperEngine::Data::Parsers::UserSettingParser;

TEST_CASE ("Strings with spaces are not parsed as vectors") {
    const Properties properties;

    const auto text = DynamicValueParser::parse (JSON ("SONG NAME"), properties, false);
    CHECK (text->getType () == DynamicValue::String);
    CHECK (text->getString () == "SONG NAME");

    const auto clock = DynamicValueParser::parse (JSON ("12:34 AM"), properties, false);
    CHECK (clock->getType () == DynamicValue::String);
    CHECK (clock->getString () == "12:34 AM");
}

TEST_CASE ("Numeric strings with spaces are parsed as vectors") {
    const Properties properties;

    const auto vec2 = DynamicValueParser::parse (JSON ("1.5 2"), properties, false);
    CHECK (vec2->getType () == DynamicValue::Vec2);
    CHECK (vec2->getVec2 () == glm::vec2 (1.5f, 2.0f));

    const auto vec3 = DynamicValueParser::parse (JSON ("1720.00000 1440.00000 0.00000"), properties, false);
    CHECK (vec3->getType () == DynamicValue::Vec3);
    CHECK (vec3->getVec3 () == glm::vec3 (1720.0f, 1440.0f, 0.0f));
}

TEST_CASE ("Scripted values keep their script and initial value") {
    const Properties properties;
    const JSON data = {
	{ "script", "export function update(value) { return value; }" },
	{ "value", "ARTIST NAME" },
    };

    const auto value = DynamicValueParser::parse (data, properties, false);
    CHECK (value->getType () == DynamicValue::String);
    CHECK (value->getString () == "ARTIST NAME");
    REQUIRE (value->getScriptSource ().has_value ());
}

TEST_CASE ("Vectors bound to scalar user properties keep their vector type") {
    Properties properties;
    const auto slider = std::make_shared<PropertySlider> (
	WallpaperEngine::Data::Model::PropertyData { .name = "scale", .text = "Scale" },
	WallpaperEngine::Data::Model::SliderData { .min = 1.0f, .max = 5.0f, .step = 0.1f }, 2.0f
    );
    properties.emplace ("scale", slider);

    const JSON data = {
	{ "user", "scale" },
	{ "value", "1.00000 1.00000 1.00000" },
    };

    const auto setting = UserSettingParser::parse (data, properties);
    CHECK (setting->value->getType () == DynamicValue::Vec3);
    CHECK (setting->value->getVec3 () == glm::vec3 (2.0f));

    // changes to the property keep propagating as vectors
    slider->update (3.0f, DynamicValue::UpdateSource::User);
    CHECK (setting->value->getType () == DynamicValue::Vec3);
    CHECK (setting->value->getVec3 () == glm::vec3 (3.0f));
}
