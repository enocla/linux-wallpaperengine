#include "ScriptableObjectAdapter.h"

#include <cstring>
#include <ranges>
#include <utility>

#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Utils;
using namespace WallpaperEngine::Scripting::Adapters;

#define SCRIPTABLE_OPAQUE_MAGIC 0xdeadbeef

namespace {
struct OpaqueScriptableObjectAdapter {
    unsigned int magic;
    ScriptableObjectAdapter& adapter;
    WallpaperEngine::Scripting::ScriptableObject& object;
};

OpaqueScriptableObjectAdapter* get_container (JSValueConst obj_val) {
    JSClassID classId = 0;
    auto* container = static_cast<OpaqueScriptableObjectAdapter*> (JS_GetAnyOpaque (obj_val, &classId));

    if (!container || container->magic != SCRIPTABLE_OPAQUE_MAGIC) {
	return nullptr;
    }

    return container;
}

JSValue scriptableobject_property_get (JSContext* ctx, JSValueConst obj_val, JSAtom atom, JSValueConst receiver) {
    auto* container = get_container (obj_val);

    if (container == nullptr) {
	return JS_UNDEFINED;
    }

    const char* name = JS_AtomToCString (ctx, atom);

    if (name == nullptr) {
	return JS_EXCEPTION;
    }

    ScopeGuard guard ([=] { JS_FreeCString (ctx, name); });

    if (auto* property = container->object.findProperty (name); property != nullptr) {
	if (WallpaperEngine::Scripting::ScriptEngine::isAngleProperty (name)) {
	    // scripts see angles in degrees, so they get a converted copy
	    return container->adapter.getEngine ().dynamicToJsCopy (DynamicValue (glm::degrees (property->getVec3 ())));
	}

	return container->adapter.getEngine ().dynamicToJs (*property);
    }

    const auto& object = container->object.getObject ();

    if (strcmp (name, "name") == 0) {
	return JS_NewString (ctx, object.name.c_str ());
    }

    if (strcmp (name, "id") == 0) {
	return JS_NewInt32 (ctx, object.id);
    }

    // exotic getters short-circuit the prototype chain, so methods have to be looked up manually
    JSValue proto = JS_GetPrototype (ctx, obj_val);
    ScopeGuard protoGuard ([=] { JS_FreeValue (ctx, proto); });

    if (!JS_IsObject (proto)) {
	return JS_UNDEFINED;
    }

    return JS_GetProperty (ctx, proto, atom);
}

int scriptableobject_property_set (
    JSContext* ctx, JSValueConst obj_val, JSAtom atom, JSValueConst val, JSValueConst receiver, int flags
) {
    auto* container = get_container (obj_val);

    if (container == nullptr) {
	return 0;
    }

    const char* name = JS_AtomToCString (ctx, atom);

    if (name == nullptr) {
	return -1;
    }

    ScopeGuard guard ([=] { JS_FreeCString (ctx, name); });

    if (auto* property = container->object.findProperty (name); property != nullptr) {
	if (WallpaperEngine::Scripting::ScriptEngine::isAngleProperty (name)) {
	    DynamicValue converted (glm::degrees (property->getVec3 ()));

	    if (container->adapter.getEngine ().assign (val, converted)) {
		property->update (glm::radians (converted.getVec3 ()), DynamicValue::UpdateSource::Script);
	    }
	} else {
	    container->adapter.getEngine ().assign (val, *property);
	}
    }

    // unknown properties are silently ignored so scripts written against newer APIs don't throw every frame
    return 1;
}

void scriptableobject_finalizer (JSRuntime* rt, JSValueConst val) {
    JSClassID classId = 0;
    auto* container = static_cast<OpaqueScriptableObjectAdapter*> (JS_GetAnyOpaque (val, &classId));

    if (!container || container->magic != SCRIPTABLE_OPAQUE_MAGIC) {
	return;
    }

    delete container;
}
} // namespace

ScriptableObjectAdapter::ScriptableObjectAdapter (ScriptEngine& engine, std::string name) :
    ObjectAdapter (engine), m_exoticMethods (
				{
				    .get_property = scriptableobject_property_get,
				    .set_property = scriptableobject_property_set,
				}
			    ),
    m_name (std::move (name)) {
    this->registerType (
	{
	    .class_name = m_name.c_str (),
	    .finalizer = scriptableobject_finalizer,
	    .exotic = &m_exoticMethods,
	}
    );
}

ScriptableObjectAdapter::~ScriptableObjectAdapter () {
    for (const auto& instance : this->m_instances | std::views::values) {
	JS_FreeValue (this->m_engine.getContext (), instance);
    }

    this->m_instances.clear ();
}

void ScriptableObjectAdapter::setPrototype (JSValue prototype) {
    JS_SetClassProto (this->m_engine.getContext (), this->m_classId, prototype);
}

JSValue ScriptableObjectAdapter::instantiate (ScriptableObject& object) {
    if (const auto it = this->m_instances.find (&object); it != this->m_instances.end ()) {
	return JS_DupValue (this->m_engine.getContext (), it->second);
    }

    JSValue result = JS_NewObjectClass (this->m_engine.getContext (), this->m_classId);
    JS_SetOpaque (
	result,
	new OpaqueScriptableObjectAdapter { .magic = SCRIPTABLE_OPAQUE_MAGIC, .adapter = *this, .object = object }
    );

    this->m_instances.emplace (&object, JS_DupValue (this->m_engine.getContext (), result));

    return result;
}

JSValue ScriptableObjectAdapter::instantiate (DynamicValue& value) {
    throw std::runtime_error ("Cannot create a ScriptableObject instance from a DynamicValue");
}
