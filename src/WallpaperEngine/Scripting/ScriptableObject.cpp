#include "ScriptableObject.h"

#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Scripting;

ScriptableObject::ScriptableObject (Wallpapers::CScene& scene, const Object& object) : CObject (scene, object) {
    // register common dynamic values, specialized objects can override these
    this->registerProperty ("origin", *object.origin->value);
    this->registerProperty ("scale", *object.groupScale->value);
    this->registerProperty ("angles", *object.groupAngles->value);
    this->registerProperty ("visible", *object.groupVisible->value);
}

DynamicValue* ScriptableObject::findProperty (const std::string& name) const {
    const auto it = this->m_properties.find (name);

    if (it == this->m_properties.end ()) {
	return nullptr;
    }

    return &it->second.value;
}

DynamicValue& ScriptableObject::getProperty (const std::string& name) {
    auto* property = this->findProperty (name);

    if (property == nullptr) {
	sLog.exception ("Property '" + name + "' not found on object '" + this->getObject ().name + "'");
    }

    return *property;
}

const std::map<std::string, ScriptableObject::PropertyEntry>& ScriptableObject::getProperties () const {
    return this->m_properties;
}

void ScriptableObject::registerProperty (const std::string& name, DynamicValue& value) {
    this->m_properties.erase (name);
    this->m_properties.emplace (
	name, PropertyEntry { .key = name + "_" + std::to_string (this->getId ()), .value = value }
    );
}
