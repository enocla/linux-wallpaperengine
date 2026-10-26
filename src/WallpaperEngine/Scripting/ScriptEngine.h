#pragma once

#include "Adapters/VectorAdapter.h"
#include "ConsoleObject.h"
#include "EngineObject.h"
#include "InputObject.h"
#include "Modules/ScriptModule.h"
#include "SceneObject.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/Media/MediaSource.h"

namespace WallpaperEngine::Media {
class MediaSource;
}
extern "C" {
#include "quickjs.h"
}

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Scripting {
class ScriptPropertiesObject;
class ScriptableObject;
namespace Adapters {
    class ScriptableObjectAdapter;
}
using namespace WallpaperEngine::Data::Model;

/**
 * Runs the scripts attached to scene properties (SceneScript).
 *
 * Every scripted property is loaded as its own ES module with `thisLayer`/`thisObject` bound to the layer that owns
 * the property. The lifecycle mimics Wallpaper Engine's:
 *  - init (value) runs once all the layers are created
 *  - applyUserProperties (properties) runs right after with every user property
 *  - update (value) runs every frame, its return value (if any) becomes the property's new value
 *  - media*Changed (event) run whenever the media integration reports changes
 */
class ScriptEngine {
public:
    struct LoadedModule {
	DynamicValue& value;
	ScriptableObject& object;
	std::string key;
	/** Name of the property the script is attached to */
	std::string property;
	JSValue module = JS_UNDEFINED;
	/** Exported functions resolved once after evaluation */
	JSValue update = JS_UNDEFINED;
	/** Amount of errors reported, used to prevent log spam from scripts failing every frame */
	int errors = 0;
    };
    struct JSObjectAdapters {
	std::unique_ptr<Adapters::VectorAdapter<4>> vec4;
	std::unique_ptr<Adapters::VectorAdapter<3>> vec3;
	std::unique_ptr<Adapters::VectorAdapter<2>> vec2;
	std::unique_ptr<Adapters::ScriptableObjectAdapter> object;
    };

    ~ScriptEngine ();
    ScriptEngine (Render::Wallpapers::CScene& scene, Media::MediaSource& mediaSource);
    ScriptEngine (const ScriptEngine&) = delete;
    ScriptEngine& operator= (const ScriptEngine&) = delete;

    JSRuntime* getRuntime () const { return m_runtime; }
    JSContext* getContext () const { return m_context; }
    JSValue getGlobalThis () const { return m_globalThis; }
    LoadedModule* getRunningModule () const { return m_runningModule; }
    /**
     * Converts the given value to javascript. Vectors are returned as live views over the value,
     * so modifying their components updates the value too.
     */
    JSValue dynamicToJs (DynamicValue& value) const;
    /**
     * Converts the given value to javascript. Vectors are copied, so scripts cannot modify the original value.
     */
    JSValue dynamicToJsCopy (const DynamicValue& value) const;
    /**
     * Updates the target value with the given javascript value, converting it to the target's type when possible.
     *
     * @return If the value was updated or not (undefined, null and unsupported types are ignored)
     */
    bool assign (JSValueConst value, DynamicValue& target) const;

    /**
     * Scene files store angles in radians, but scripts work with degrees
     *
     * @return If the given layer property is exposed to scripts in degrees
     */
    static bool isAngleProperty (const std::string& property) { return property == "angles"; }

    /**
     * Loads all the scripts attached to properties of the scene's layers and runs their init() and
     * applyUserProperties() functions. Must be called once all the scene objects are created.
     */
    void initialize ();

    /**
     * Runs a frame tick in the javascript engine. Dispatches any pending events,
     * timeouts, intervals AND calls any update() functions.
     */
    void tick ();

    const JSObjectAdapters& getAdapters () const { return m_adapters; }
    const Render::Wallpapers::CScene& getScene () const { return m_scene; }
    const std::map<std::string, std::unique_ptr<Modules::ScriptModule>>& getModules () const { return m_modules; }

private:
    JSValue call (JSValue module, int argc, JSValueConst argv[], const char* name);
    void
    loadModule (ScriptableObject& object, const std::string& property, const std::string& key, DynamicValue& value);
    /**
     * Calls the given function of the module with the module's value as argument and applies the result
     */
    void callWithValue (LoadedModule& module, JSValueConst function, const char* name);
    /**
     * Calls the given exported function on every module that defines it
     */
    void broadcast (const char* name, JSValueConst argument);
    void reportException (LoadedModule& module, const char* function);
    void runPendingJobs ();

    void installBuiltins ();

    void notifyMediaUpdate (const Media::MediaSource::MediaInfo& media, bool force);

    JSRuntime* m_runtime = nullptr;
    JSContext* m_context = nullptr;
    JSValue m_globalThis;
    Render::Wallpapers::CScene& m_scene;
    std::unique_ptr<EngineObject> m_engineObject;
    std::unique_ptr<InputObject> m_inputObject;
    std::unique_ptr<SceneObject> m_sceneObject;
    std::unique_ptr<ConsoleObject> m_consoleObject;
    std::unique_ptr<ScriptPropertiesObject> m_scriptPropertiesObject;

    std::map<std::string, std::unique_ptr<Modules::ScriptModule>> m_modules = {};
    /** Loaded modules in scene order, the order matters as update () is called in the same order as WE does */
    std::vector<std::unique_ptr<LoadedModule>> m_scriptModules = {};

    LoadedModule* m_runningModule = nullptr;

    bool m_builtinsInstalled = false;
    bool m_initialized = false;
    Media::MediaSource& m_mediaSource;
    /** The last media state sent to the scripts, used to only notify of actual changes */
    std::optional<Media::MediaSource::MediaInfo> m_lastMedia;
    std::function<void ()> m_unregisterMediaUpdateCallback;
    std::function<void ()> m_unregisterAlbumArtUpdateCallback;

    JSObjectAdapters m_adapters;
};
} // namespace WallpaperEngine::Scripting
