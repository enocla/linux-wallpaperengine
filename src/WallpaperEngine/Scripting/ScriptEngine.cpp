#include "ScriptEngine.h"

#include "Adapters/ScriptableObjectAdapter.h"
#include "Modules/ColorModule.h"
#include "Modules/MathModule.h"
#include "Modules/ScriptModule.h"
#include "ScriptPropertiesObject.h"
#include "ScriptableObject.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include "WallpaperEngine/Scripting/Builtins.generated.h"
#include "quickjs.h"

#include <cmath>
#include <cstring>
#include <ranges>
#include <string_view>
#include <vector>

using namespace WallpaperEngine::Scripting;
using namespace WallpaperEngine::Data::Model;

namespace {
/** Maximum amount of errors logged per module, scripts failing every frame would otherwise flood the log */
constexpr int kMaxReportedErrors = 3;
/** MPRIS reports times in microseconds, scripts expect seconds */
constexpr double kMicrosecondsPerSecond = 1000000.0;

JSModuleDef* scriptengine_module_loader (JSContext* ctx, const char* module, void* opaque) {
    const auto* scriptEngine = static_cast<ScriptEngine*> (opaque);

    const auto& modules = scriptEngine->getModules ();
    const auto it = modules.find (module);

    if (it == modules.end ()) {
	JS_ThrowReferenceError (ctx, "could not load module '%s'", module);
	return nullptr;
    }

    return it->second->getDefinition ();
}

std::string exceptionToString (JSContext* ctx, JSValueConst exception) {
    std::string result;
    const char* str = JS_ToCString (ctx, exception);

    if (str != nullptr) {
	result = str;
	JS_FreeCString (ctx, str);
    }

    // include the stack when available, makes it way easier to find what failed
    if (JS_IsError (exception)) {
	JSValue stack = JS_GetPropertyStr (ctx, exception, "stack");

	if (JS_IsString (stack)) {
	    const char* stackStr = JS_ToCString (ctx, stack);

	    if (stackStr != nullptr) {
		result += "\n";
		result += stackStr;
		JS_FreeCString (ctx, stackStr);
	    }
	}

	JS_FreeValue (ctx, stack);
    }

    return result;
}

/** Logs and clears the pending exception */
void logJSException (JSContext* ctx, const std::string& context) {
    JSValue exception = JS_GetException (ctx);

    if (!JS_IsNull (exception) && !JS_IsUndefined (exception)) {
	sLog.error ("ScriptEngine [", context, "]: ", exceptionToString (ctx, exception));
    }

    JS_FreeValue (ctx, exception);
}

/**
 * Reads the numeric components of a javascript vector-like object
 *
 * @return The amount of components read (0 if the value is not a vector)
 */
int readVector (JSContext* ctx, JSValueConst value, glm::vec4& out) {
    static constexpr const char* names[] = { "x", "y", "z", "w" };
    int count = 0;

    for (int i = 0; i < 4; i++) {
	JSValue component = JS_GetPropertyStr (ctx, value, names[i]);

	if (!JS_IsNumber (component)) {
	    JS_FreeValue (ctx, component);
	    break;
	}

	double result = 0.0;
	JS_ToFloat64 (ctx, &result, component);
	JS_FreeValue (ctx, component);
	out[i] = static_cast<float> (result);
	count++;
    }

    // a vector needs at least two components
    return count >= 2 ? count : 0;
}
} // namespace

JSValue ScriptEngine::dynamicToJs (DynamicValue& value) const {
    switch (value.getType ()) {
	case DynamicValue::Null:
	    return JS_NULL;
	case DynamicValue::String:
	    return JS_NewString (this->m_context, value.getString ().c_str ());
	case DynamicValue::Float:
	    return JS_NewFloat64 (this->m_context, value.getFloat ());
	case DynamicValue::Int:
	    return JS_NewInt32 (this->m_context, value.getInt ());
	case DynamicValue::Boolean:
	    return JS_NewBool (this->m_context, value.getBool ());
	case DynamicValue::Vec2:
	    return this->m_adapters.vec2->instantiate (value);
	case DynamicValue::Vec3:
	    return this->m_adapters.vec3->instantiate (value);
	case DynamicValue::Vec4:
	    return this->m_adapters.vec4->instantiate (value);
	default:
	    return JS_UNDEFINED;
    }
}

JSValue ScriptEngine::dynamicToJsCopy (const DynamicValue& value) const {
    // the temporal instantiate makes a copy of the value, so the const_cast is safe
    auto& source = const_cast<DynamicValue&> (value);

    switch (value.getType ()) {
	case DynamicValue::Vec2:
	    return this->m_adapters.vec2->instantiate (source, true);
	case DynamicValue::Vec3:
	    return this->m_adapters.vec3->instantiate (source, true);
	case DynamicValue::Vec4:
	    return this->m_adapters.vec4->instantiate (source, true);
	default:
	    return this->dynamicToJs (source);
    }
}

bool ScriptEngine::assign (JSValueConst value, DynamicValue& target) const {
    JSContext* ctx = this->m_context;
    constexpr auto source = DynamicValue::UpdateSource::Script;

    if (JS_IsException (value) || JS_IsUndefined (value) || JS_IsNull (value) || JS_IsUninitialized (value)) {
	return false;
    }

    if (JS_IsBool (value)) {
	target.update (static_cast<bool> (JS_ToBool (ctx, value)), source);
	return true;
    }

    if (JS_IsNumber (value)) {
	double number = 0.0;
	JS_ToFloat64 (ctx, &number, value);

	switch (target.getType ()) {
	    case DynamicValue::Boolean:
		target.update (number != 0.0, source);
		break;
	    case DynamicValue::Int:
		target.update (static_cast<int> (number), source);
		break;
	    case DynamicValue::String:
		{
		    // keep strings as strings, but avoid the trailing zeroes std::to_string adds
		    const char* str = JS_ToCString (ctx, value);
		    target.update (std::string (str ? str : ""), source);
		    JS_FreeCString (ctx, str);
		}
		break;
	    case DynamicValue::Vec2:
		target.update (glm::vec2 (static_cast<float> (number)), source);
		break;
	    case DynamicValue::Vec3:
		target.update (glm::vec3 (static_cast<float> (number)), source);
		break;
	    case DynamicValue::Vec4:
		target.update (glm::vec4 (static_cast<float> (number)), source);
		break;
	    default:
		target.update (static_cast<float> (number), source);
		break;
	}

	return true;
    }

    if (JS_IsString (value)) {
	const char* str = JS_ToCString (ctx, value);
	target.update (std::string (str ? str : ""), source);
	JS_FreeCString (ctx, str);
	return true;
    }

    if (!JS_IsObject (value)) {
	return false;
    }

    // arrays of strings are commonly used in place of strings by some scripts
    if (JS_IsArray (value) && target.getType () == DynamicValue::String) {
	const char* str = JS_ToCString (ctx, value);
	target.update (std::string (str ? str : ""), source);
	JS_FreeCString (ctx, str);
	return true;
    }

    glm::vec4 vector = target.getVec4 ();
    const int components = readVector (ctx, value, vector);

    if (components == 0) {
	return false;
    }

    // keep the target's dimensions, missing components keep their current value
    switch (target.getType ()) {
	case DynamicValue::Vec2:
	    target.update (glm::vec2 (vector), source);
	    break;
	case DynamicValue::Vec4:
	    target.update (vector, source);
	    break;
	case DynamicValue::Vec3:
	    target.update (glm::vec3 (vector), source);
	    break;
	default:
	    if (components == 2) {
		target.update (glm::vec2 (vector), source);
	    } else if (components == 3) {
		target.update (glm::vec3 (vector), source);
	    } else {
		target.update (vector, source);
	    }
	    break;
    }

    return true;
}

ScriptEngine::ScriptEngine (Render::Wallpapers::CScene& scene, Media::MediaSource& mediaSource) :
    m_scene (scene), m_mediaSource (mediaSource) {
    this->m_unregisterMediaUpdateCallback
	= mediaSource.addMetadataListener ([this] (const Media::MediaSource::MediaInfo& info) {
	      this->notifyMediaUpdate (info, false);
	  });

    this->m_unregisterAlbumArtUpdateCallback
	= mediaSource.addAlbumArtListener ([this] (const Media::MediaSource::MediaInfo& info) {
	      this->notifyMediaUpdate (info, false);
	  });

    this->m_runtime = JS_NewRuntime ();

    if (!this->m_runtime) {
	sLog.exception ("ScriptEngine: Failed to create JS runtime");
    }

    this->m_context = JS_NewContext (this->m_runtime);

    if (!this->m_context) {
	JS_FreeRuntime (this->m_runtime);
	sLog.exception ("ScriptEngine: Failed to create JS context");
    }

    this->m_globalThis = JS_GetGlobalObject (this->m_context);

    this->m_adapters = {
	.vec4 = std::unique_ptr<Adapters::VectorAdapter<4>> (new Adapters::VectorAdapter<4> (*this)),
	.vec3 = std::unique_ptr<Adapters::VectorAdapter<3>> (new Adapters::VectorAdapter<3> (*this)),
	.vec2 = std::unique_ptr<Adapters::VectorAdapter<2>> (new Adapters::VectorAdapter<2> (*this)),
	.object
	= std::unique_ptr<Adapters::ScriptableObjectAdapter> (new Adapters::ScriptableObjectAdapter (*this, "ILayer")),
    };

    this->m_engineObject = std::make_unique<EngineObject> (*this, scene);
    this->m_inputObject = std::make_unique<InputObject> (*this, scene);
    this->m_sceneObject = std::make_unique<SceneObject> (*this, scene);
    this->m_consoleObject = std::make_unique<ConsoleObject> (*this, scene);
    this->m_scriptPropertiesObject = std::make_unique<ScriptPropertiesObject> (*this, scene);

    auto wemath = std::make_unique<Modules::MathModule> (*this);
    auto wecolor = std::make_unique<Modules::ColorModule> (*this);

    this->m_modules.emplace (wemath->getName (), std::move (wemath));
    this->m_modules.emplace (wecolor->getName (), std::move (wecolor));

    JS_SetModuleLoaderFunc (this->m_runtime, nullptr, scriptengine_module_loader, this);
    // setup scene objects and other things
    this->installBuiltins ();
    // add engine to the global
    JS_DefinePropertyValueStr (
	this->m_context, this->m_globalThis, "engine", this->m_engineObject->getInstance (), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_context, this->m_globalThis, "input", this->m_inputObject->getInstance (), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_context, this->m_globalThis, "thisScene", this->m_sceneObject->getInstance (), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_context, this->m_globalThis, "console", this->m_consoleObject->getInstance (), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_context, this->m_globalThis, "shared", JS_NewObject (this->m_context), JS_PROP_ENUMERABLE
    );
}

ScriptEngine::~ScriptEngine () {
    this->m_unregisterMediaUpdateCallback ();
    this->m_unregisterAlbumArtUpdateCallback ();

    for (const auto& module : this->m_scriptModules) {
	JS_FreeValue (this->m_context, module->update);
	JS_FreeValue (this->m_context, module->module);
    }

    JS_FreeValue (this->m_context, this->m_globalThis);

    this->m_adapters.object.reset ();
    this->m_adapters.vec4.reset ();
    this->m_adapters.vec3.reset ();
    this->m_adapters.vec2.reset ();

    this->m_consoleObject.reset ();
    this->m_engineObject.reset ();
    this->m_inputObject.reset ();
    this->m_sceneObject.reset ();
    this->m_scriptPropertiesObject.reset ();
    this->m_modules.clear ();
    this->m_scriptModules.clear ();

    if (this->m_context) {
	JS_FreeContext (this->m_context);
    }
    if (this->m_runtime) {
	JS_FreeRuntime (this->m_runtime);
    }
}

void ScriptEngine::installBuiltins () {
    if (this->m_builtinsInstalled || !this->m_context) {
	return;
    }

    JSValue result = JS_Eval (
	this->m_context, SCENE_SCRIPT_BUILTINS, strlen (SCENE_SCRIPT_BUILTINS), "<scene-script-builtins>",
	JS_EVAL_TYPE_GLOBAL
    );
    if (JS_IsException (result)) {
	logJSException (this->m_context, "installBuiltins");
    }
    JS_FreeValue (this->m_context, result);

    // ILayer instances use the helpers defined in javascript as prototype
    JSValue layerPrototype = JS_GetPropertyStr (this->m_context, this->m_globalThis, "__wpeLayerPrototype");

    if (JS_IsObject (layerPrototype)) {
	this->m_adapters.object->setPrototype (layerPrototype);
    } else {
	JS_FreeValue (this->m_context, layerPrototype);
    }

    this->m_builtinsInstalled = true;
}

JSValue ScriptEngine::call (JSValue module, int argc, JSValue argv[], const char* name) {
    if (!JS_IsObject (module)) {
	return JS_UNDEFINED;
    }

    JSValue function = JS_GetPropertyStr (this->m_context, module, name);
    ScopeGuard guard ([&] () { JS_FreeValue (this->m_context, function); });

    if (!JS_IsFunction (this->m_context, function)) {
	return JS_UNDEFINED;
    }

    return JS_Call (this->m_context, function, module, argc, argv);
}

void ScriptEngine::reportException (LoadedModule& module, const char* function) {
    if (++module.errors > kMaxReportedErrors) {
	// still need to clear the exception
	JS_FreeValue (this->m_context, JS_GetException (this->m_context));
	return;
    }

    std::string context = module.key + "@" + module.object.getObject ().name + ":" + function;

    if (module.errors == kMaxReportedErrors) {
	context += " (further errors of this script are not reported)";
    }

    logJSException (this->m_context, context);
}

void ScriptEngine::runPendingJobs () {
    JSContext* ctx = nullptr;
    int result = 0;

    while ((result = JS_ExecutePendingJob (this->m_runtime, &ctx)) != 0) {
	if (result < 0 && ctx != nullptr) {
	    logJSException (ctx, "pendingJob");
	}
    }
}

void ScriptEngine::loadModule (
    ScriptableObject& object, const std::string& property, const std::string& key, DynamicValue& value
) {
    const auto& script = value.getScriptSource ();

    if (!script.has_value ()) {
	return;
    }

    JSContext* ctx = this->m_context;
    auto& module = *this->m_scriptModules.emplace_back (
	std::make_unique<LoadedModule> (LoadedModule {
	    .value = value, .object = object, .key = key, .property = property })
    );

    // every module gets its own thisLayer/thisObject bindings, captured at evaluation time so closures (timeouts,
    // intervals, event handlers...) keep referencing the right layer
    std::string source = "const thisLayer = globalThis.__wpeThisLayer; const thisObject = thisLayer;\n";
    source += *script;

    JS_SetPropertyStr (ctx, this->m_globalThis, "__wpeThisLayer", this->m_adapters.object->instantiate (object));
    JS_SetPropertyStr (
	ctx, this->m_globalThis, "__wpeScriptProperties", this->m_scriptPropertiesObject->instantiate (value)
    );

    this->m_runningModule = &module;

    ScopeGuard cleanup ([this, ctx] {
	JS_SetPropertyStr (ctx, this->m_globalThis, "__wpeThisLayer", JS_UNDEFINED);
	JS_SetPropertyStr (ctx, this->m_globalThis, "__wpeScriptProperties", JS_UNDEFINED);
	this->m_runningModule = nullptr;
    });

    JSValue compiled
	= JS_Eval (ctx, source.c_str (), source.size (), key.c_str (), JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

    if (JS_IsException (compiled)) {
	this->reportException (module, "compile");
	return;
    }

    auto* definition = static_cast<JSModuleDef*> (JS_VALUE_GET_PTR (compiled));
    // evaluating a module returns a promise in quickjs-ng
    JSValue result = JS_EvalFunction (ctx, compiled);

    if (JS_IsException (result)) {
	this->reportException (module, "evaluate");
	return;
    }

    this->runPendingJobs ();

    if (JS_IsPromise (result) && JS_PromiseState (ctx, result) == JS_PROMISE_REJECTED) {
	JS_Throw (ctx, JS_PromiseResult (ctx, result));
	JS_FreeValue (ctx, result);
	this->reportException (module, "evaluate");
	return;
    }

    JS_FreeValue (ctx, result);

    module.module = JS_GetModuleNamespace (ctx, definition);
    module.update = JS_GetPropertyStr (ctx, module.module, "update");

    if (!JS_IsFunction (ctx, module.update)) {
	JS_FreeValue (ctx, module.update);
	module.update = JS_UNDEFINED;
    }
}

void ScriptEngine::callWithValue (LoadedModule& module, JSValueConst function, const char* name) {
    JSContext* ctx = this->m_context;
    const bool degrees = isAngleProperty (module.property);
    // angles are handed to the script in degrees, the script works on a copy that is converted back afterwards
    DynamicValue converted (glm::degrees (module.value.getVec3 ()));
    DynamicValue& value = degrees ? converted : module.value;

    this->m_runningModule = &module;

    JSValue args[] = { this->dynamicToJs (value) };
    JSValue result = JS_Call (ctx, function, module.module, 1, args);

    if (JS_IsException (result)) {
	this->reportException (module, name);
    } else {
	// returning nothing keeps the current value (or whatever the script changed in place)
	this->assign (result, value);

	if (degrees && glm::radians (converted.getVec3 ()) != module.value.getVec3 ()) {
	    module.value.update (glm::radians (converted.getVec3 ()), DynamicValue::UpdateSource::Script);
	}
    }

    JS_FreeValue (ctx, result);
    JS_FreeValue (ctx, args[0]);
    this->m_runningModule = nullptr;
}

void ScriptEngine::broadcast (const char* name, JSValueConst argument) {
    JSValue args[] = { argument };

    for (const auto& module : this->m_scriptModules) {
	if (!JS_IsObject (module->module)) {
	    continue;
	}

	this->m_runningModule = module.get ();
	JSValue result = this->call (module->module, 1, args, name);

	if (JS_IsException (result)) {
	    this->reportException (*module, name);
	}

	JS_FreeValue (this->m_context, result);
    }

    this->m_runningModule = nullptr;
    this->runPendingJobs ();
}

void ScriptEngine::initialize () {
    if (this->m_initialized) {
	return;
    }

    this->m_initialized = true;
    JSContext* ctx = this->m_context;

    // load every script attached to the layers' properties
    for (auto* object : this->m_scene.getObjectsByRenderOrder ()) {
	if (!object->is<ScriptableObject> ()) {
	    continue;
	}

	auto* scriptable = object->as<ScriptableObject> ();

	for (const auto& [name, entry] : scriptable->getProperties ()) {
	    this->loadModule (*scriptable, name, entry.key, entry.value);
	}
    }

    sLog.out ("ScriptEngine: loaded ", this->m_scriptModules.size (), " scripts");

    // init (value) runs once every layer is available
    for (const auto& module : this->m_scriptModules) {
	if (!JS_IsObject (module->module)) {
	    continue;
	}

	JSValue init = JS_GetPropertyStr (ctx, module->module, "init");

	if (JS_IsFunction (ctx, init)) {
	    this->callWithValue (*module, init, "init");
	}

	JS_FreeValue (ctx, init);
    }

    this->m_runningModule = nullptr;

    // applyUserProperties receives every user property on startup
    JSValue properties = JS_NewObject (ctx);

    for (const auto& [name, property] : this->m_scene.getScene ().project.properties) {
	if (property == nullptr || property->getType () == DynamicValue::Null) {
	    continue;
	}

	JS_SetPropertyStr (ctx, properties, name.c_str (), this->dynamicToJsCopy (*property));
    }

    this->broadcast ("applyUserProperties", properties);
    JS_FreeValue (ctx, properties);

    // let scripts know about the current media state
    this->notifyMediaUpdate (this->m_mediaSource.getMediaInfo (), true);
}

void ScriptEngine::tick () {
    // run intervals
    this->m_engineObject->tick ();

    // run all update methods
    for (const auto& module : this->m_scriptModules) {
	if (JS_IsUndefined (module->update)) {
	    continue;
	}

	this->callWithValue (*module, module->update, "update");
    }

    this->m_runningModule = nullptr;
    this->runPendingJobs ();
}

void ScriptEngine::notifyMediaUpdate (const Media::MediaSource::MediaInfo& media, bool force) {
    // events are only dispatched once the scripts are loaded, initialize () sends the current state
    if (!this->m_initialized) {
	return;
    }

    JSContext* ctx = this->m_context;
    const auto& last = this->m_lastMedia;

    const bool propertiesChanged = force || !last.has_value () || last->title != media.title
	|| last->artist != media.artist || last->album != media.album;
    const bool playbackChanged = force || !last.has_value () || last->playbackState != media.playbackState;
    const bool timelineChanged
	= force || !last.has_value () || last->position != media.position || last->duration != media.duration;
    const bool thumbnailChanged = force || !last.has_value () || last->url != media.url;

    this->m_lastMedia = media;

    if (force) {
	JSValue statusEvent = JS_NewObject (ctx);
	JS_SetPropertyStr (ctx, statusEvent, "enabled", JS_TRUE);
	this->broadcast ("mediaStatusChanged", statusEvent);
	JS_FreeValue (ctx, statusEvent);
    }

    if (propertiesChanged) {
	JSValue event = JS_NewObject (ctx);

	JS_SetPropertyStr (ctx, event, "title", JS_NewString (ctx, media.title.c_str ()));
	JS_SetPropertyStr (ctx, event, "artist", JS_NewString (ctx, media.artist.c_str ()));
	JS_SetPropertyStr (ctx, event, "albumTitle", JS_NewString (ctx, media.album.c_str ()));
	JS_SetPropertyStr (ctx, event, "albumArtist", JS_NewString (ctx, ""));
	JS_SetPropertyStr (ctx, event, "subTitle", JS_NewString (ctx, ""));
	JS_SetPropertyStr (ctx, event, "genres", JS_NewString (ctx, ""));
	JS_SetPropertyStr (ctx, event, "contentType", JS_NewString (ctx, "music"));

	this->broadcast ("mediaPropertiesChanged", event);
	JS_FreeValue (ctx, event);
    }

    if (thumbnailChanged) {
	DynamicValue primaryColorValue (glm::vec3 (0.12f, 0.12f, 0.12f));
	DynamicValue secondaryColorValue (glm::vec3 (0.0f, 0.0f, 0.0f));
	DynamicValue tertiaryColorValue (glm::vec3 (0.25f, 0.25f, 0.25f));
	DynamicValue textColorValue (glm::vec3 (1.0f, 1.0f, 1.0f));
	DynamicValue highContrastColorValue (glm::vec3 (1.0f, 1.0f, 1.0f));

	// TODO: PROCESS THESE COLORS FROM THE ALBUM ART INSTEAD OF HARDCODING THEM
	JSValue event = JS_NewObject (ctx);

	JS_SetPropertyStr (
	    ctx, event, "hasThumbnail", JS_NewBool (ctx, media.url.has_value () && !media.url->empty ())
	);
	JS_SetPropertyStr (ctx, event, "primaryColor", this->dynamicToJsCopy (primaryColorValue));
	JS_SetPropertyStr (ctx, event, "secondaryColor", this->dynamicToJsCopy (secondaryColorValue));
	JS_SetPropertyStr (ctx, event, "tertiaryColor", this->dynamicToJsCopy (tertiaryColorValue));
	JS_SetPropertyStr (ctx, event, "textColor", this->dynamicToJsCopy (textColorValue));
	JS_SetPropertyStr (ctx, event, "highContrastColor", this->dynamicToJsCopy (highContrastColorValue));

	this->broadcast ("mediaThumbnailChanged", event);
	JS_FreeValue (ctx, event);
    }

    if (playbackChanged) {
	JSValue event = JS_NewObject (ctx);
	JS_SetPropertyStr (ctx, event, "state", JS_NewInt32 (ctx, media.playbackState));
	this->broadcast ("mediaPlaybackChanged", event);
	JS_FreeValue (ctx, event);
    }

    if (timelineChanged) {
	JSValue event = JS_NewObject (ctx);
	JS_SetPropertyStr (ctx, event, "position", JS_NewFloat64 (ctx, media.position / kMicrosecondsPerSecond));
	// whole seconds, scripts usually format this directly as minutes:seconds
	JS_SetPropertyStr (
	    ctx, event, "duration", JS_NewFloat64 (ctx, std::round (media.duration / kMicrosecondsPerSecond))
	);
	this->broadcast ("mediaTimelineChanged", event);
	JS_FreeValue (ctx, event);
    }
}
