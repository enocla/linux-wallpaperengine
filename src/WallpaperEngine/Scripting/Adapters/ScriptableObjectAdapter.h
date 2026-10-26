#pragma once

#include "ObjectAdapter.h"

#include <map>
#include <string>

namespace WallpaperEngine::Scripting::Adapters {
/**
 * Exposes ScriptableObjects (scene layers) to javascript as ILayer instances.
 *
 * Instances are cached per object, so repeated thisScene.getLayer() calls don't allocate new wrappers every frame.
 */
class ScriptableObjectAdapter : public ObjectAdapter {
public:
    explicit ScriptableObjectAdapter (ScriptEngine& engine, std::string name);
    ~ScriptableObjectAdapter () override;

    JSValue instantiate (ScriptableObject& object) override;
    JSValue instantiate (Data::Model::DynamicValue& value) override;

    /**
     * Sets the prototype used by ILayer instances (holds the javascript-side helper methods)
     */
    void setPrototype (JSValue prototype);

private:
    JSClassExoticMethods m_exoticMethods;
    std::string m_name;
    std::map<ScriptableObject*, JSValue> m_instances;
};
}
