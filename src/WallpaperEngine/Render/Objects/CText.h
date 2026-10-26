#pragma once

#include <string>
#include <vector>

#include <GL/glew.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

// Forward-declare FreeType types to avoid leaking the header into users.
struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Data::Model;

/**
 * Text renderer.
 *
 * Renders text objects as a single FreeType-rasterized texture drawn on a textured quad with its own minimal GLSL
 * shader. Does NOT go through CRenderable / materials / passes, so effects on text layers are not supported.
 *
 * The text, alpha, color, origin... are exposed to the ScriptEngine, which takes care of running any script attached
 * to them, this class only re-rasterizes the text whenever the value changes.
 */
class CText final : virtual public CObject, public Scripting::ScriptableObject {
public:
    CText (Wallpapers::CScene& scene, const Text& text);
    ~CText () override;

    void setup () override;
    void render () override;

private:
    // Rebuilds the glyph texture (and matching quad VBO) from the given string.
    // Reuses existing GL handles if already allocated, so this is safe to call
    // every time the rendered text changes.
    void rebuildTextureFrom (const std::string& text);
    void buildShader ();
    void uploadQuadVertices ();

    // setup() helpers (kept small to keep the setup flow linear).
    bool initFreeType ();
    bool loadFontFromAssets (const std::string& font);
    bool loadSystemFont ();
    unsigned int computePixelSize () const;

    const Text& m_text;
    std::string m_lastRenderedText;
    unsigned int m_lastPixelSize = 0;

    /** Size of the rendered text in scene units (before scaling), exposed to scripts as `size` */
    DynamicValue m_sizeValue;
    /** Exposed to scripts as `maxwidth` */
    DynamicValue m_maxWidthValue;

    FT_Library m_ftLibrary = nullptr;
    FT_Face m_ftFace = nullptr;
    std::vector<uint8_t> m_fontData;

    GLuint m_texture = 0;
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_uMVP = -1;
    GLint m_uColor = -1;
    GLint m_uTexture = -1;

    glm::ivec2 m_textureSize = { 0, 0 };
    glm::vec2 m_quadSize = { 0.0f, 0.0f };

    bool m_valid = false;
};
} // namespace WallpaperEngine::Render::Objects
