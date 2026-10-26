#pragma once
#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/Render/CObject.h"

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Scripting {
class ScriptableObject : virtual public CObject {
public:
    struct PropertyEntry {
	std::string key;
	DynamicValue& value;
    };

    ScriptableObject (Wallpapers::CScene& scene, const Object& object);
    virtual ~ScriptableObject () = default;

    /**
     * @return The property registered under the given name or nullptr if it does not exist
     */
    [[nodiscard]] DynamicValue* findProperty (const std::string& name) const;
    DynamicValue& getProperty (const std::string& name);

    const std::map<std::string, PropertyEntry>& getProperties () const;

protected:
    /**
     * Exposes the given value to scripts under the given name. Registering the same name again replaces the previous
     * entry, which allows specialized objects (images, texts...) to override the generic group transforms registered
     * by this base class with their own values.
     *
     * Scripts attached to these values are not evaluated here, the ScriptEngine picks them up once the whole scene
     * has been created so scripts can access every layer from their init() functions.
     */
    void registerProperty (const std::string& name, DynamicValue& value);

private:
    std::map<std::string, PropertyEntry> m_properties;
};
}
