#include "CText.h"

#include <algorithm>
#include <filesystem>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Model/UserSetting.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Camera.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

using namespace WallpaperEngine::Render::Objects;

namespace {
/**
 * Wallpaper Engine rasterizes text at four scene units per point
 * (e.g. a 20pt label is 80 units tall in a 1440 units tall scene)
 */
constexpr float kSceneUnitsPerPoint = 4.0f;

/** Font shipped with Wallpaper Engine's assets, used when the wallpaper's font cannot be loaded */
const char* kAssetsFallbackFont = "fonts/NotoSans-Regular.ttf";

const std::vector<std::string> kFontCandidates = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/run/current-system/sw/share/X11/fonts/DejaVuSans.ttf",
};

const char* kVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* kFragmentShader = R"glsl(
#version 330 core
in vec2 vUV;
uniform sampler2D uTexture;
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    float coverage = texture(uTexture, vUV).r;
    FragColor = vec4(uColor.rgb, uColor.a * coverage);
}
)glsl";

GLuint compileShader (GLenum type, const char* source) {
    GLuint shader = glCreateShader (type);
    glShaderSource (shader, 1, &source, nullptr);
    glCompileShader (shader);

    GLint status = GL_FALSE;
    glGetShaderiv (shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
	char log[1024];
	glGetShaderInfoLog (shader, sizeof (log), nullptr, log);
	sLog.error ("CText shader compile failed: ", log);
	glDeleteShader (shader);
	return 0;
    }
    return shader;
}

/**
 * Decodes an UTF-8 string into codepoints, invalid sequences are replaced with U+FFFD
 */
std::vector<FT_ULong> decodeUtf8 (const std::string& text) {
    std::vector<FT_ULong> result;
    result.reserve (text.size ());

    for (size_t i = 0; i < text.size ();) {
	const auto c = static_cast<unsigned char> (text[i]);
	int length = 0;
	FT_ULong codepoint = 0;

	if (c < 0x80) {
	    codepoint = c;
	    length = 1;
	} else if ((c & 0xE0) == 0xC0) {
	    codepoint = c & 0x1F;
	    length = 2;
	} else if ((c & 0xF0) == 0xE0) {
	    codepoint = c & 0x0F;
	    length = 3;
	} else if ((c & 0xF8) == 0xF0) {
	    codepoint = c & 0x07;
	    length = 4;
	} else {
	    result.push_back (0xFFFD);
	    i++;
	    continue;
	}

	if (i + length > text.size ()) {
	    result.push_back (0xFFFD);
	    break;
	}

	bool valid = true;

	for (int j = 1; j < length; j++) {
	    const auto next = static_cast<unsigned char> (text[i + j]);

	    if ((next & 0xC0) != 0x80) {
		valid = false;
		break;
	    }

	    codepoint = (codepoint << 6) | (next & 0x3F);
	}

	result.push_back (valid ? codepoint : 0xFFFD);
	i += valid ? length : 1;
    }

    return result;
}
} // namespace

CText::CText (Wallpapers::CScene& scene, const Text& text) :
    CObject (scene, text), ScriptableObject (scene, text), m_text (text), m_sizeValue (text.size),
    m_maxWidthValue (text.maxWidth) {
    this->registerProperty ("color", *text.color->value);
    this->registerProperty ("alpha", *text.alpha->value);
    this->registerProperty ("origin", *text.origin->value);
    this->registerProperty ("scale", *text.scale->value);
    this->registerProperty ("visible", *text.visible->value);
    this->registerProperty ("pointsize", *text.pointSize->value);
    this->registerProperty ("pointSize", *text.pointSize->value);
    this->registerProperty ("text", *text.text->value);
    this->registerProperty ("size", this->m_sizeValue);
    this->registerProperty ("maxwidth", this->m_maxWidthValue);
}

CText::~CText () {
    if (m_vbo != 0) {
	glDeleteBuffers (1, &m_vbo);
    }
    if (m_vao != 0) {
	glDeleteVertexArrays (1, &m_vao);
    }
    if (m_program != 0) {
	glDeleteProgram (m_program);
    }
    if (m_texture != 0) {
	glDeleteTextures (1, &m_texture);
    }
    if (m_ftFace != nullptr) {
	FT_Done_Face (m_ftFace);
    }
    if (m_ftLibrary != nullptr) {
	FT_Done_FreeType (m_ftLibrary);
    }
}

void CText::setup () {
    if (!initFreeType ()) {
	return;
    }

    // `systemfont_*` references signal "use a system font"
    const bool systemFont = m_text.font.empty () || m_text.font.rfind ("systemfont_", 0) == 0;

    if ((systemFont || !loadFontFromAssets (m_text.font)) && !loadFontFromAssets (kAssetsFallbackFont)
	&& !loadSystemFont ()) {
	sLog.error ("CText: no usable font found for '", m_text.name, "'");
	return;
    }

    m_lastPixelSize = computePixelSize ();
    FT_Set_Pixel_Sizes (m_ftFace, 0, static_cast<FT_UInt> (m_lastPixelSize));

    buildShader ();
    rebuildTextureFrom (m_text.text->value->getString ());

    m_valid = m_texture != 0 && m_program != 0 && m_vao != 0;
}

bool CText::initFreeType () {
    if (FT_Init_FreeType (&m_ftLibrary) == 0) {
	return true;
    }
    sLog.error ("CText: FT_Init_FreeType failed for object ", m_text.name);
    return false;
}

bool CText::loadFontFromAssets (const std::string& font) {
    // Wallpapers packed in .pkg don't expose physical paths, so we read the font
    // into memory and use FT_New_Memory_Face. m_fontData must outlive the face.
    try {
	auto stream = getAssetLocator ().read (font);
	stream->seekg (0, std::ios::end);
	const auto size = stream->tellg ();
	stream->seekg (0, std::ios::beg);
	m_fontData.resize (static_cast<size_t> (size));
	stream->read (reinterpret_cast<char*> (m_fontData.data ()), size);

	if (FT_New_Memory_Face (
		m_ftLibrary, m_fontData.data (), static_cast<FT_Long> (m_fontData.size ()), 0, &m_ftFace
	    )
	    == 0) {
	    return true;
	}

	sLog.error ("CText: FT_New_Memory_Face failed for '", font, "'");
    } catch (const std::exception&) {
	sLog.error ("CText: cannot find font '", font, "' for '", m_text.name, "', using a fallback font");
    }

    m_fontData.clear ();
    return false;
}

bool CText::loadSystemFont () {
    for (const auto& candidate : kFontCandidates) {
	if (std::filesystem::exists (candidate) && FT_New_Face (m_ftLibrary, candidate.c_str (), 0, &m_ftFace) == 0) {
	    return true;
	}
    }

    return false;
}

unsigned int CText::computePixelSize () const {
    // glyphs are rasterized directly in scene units, the object's scale is applied when rendering
    return std::max<unsigned int> (
	1u, static_cast<unsigned int> (std::round (m_text.pointSize->value->getFloat () * kSceneUnitsPerPoint))
    );
}

void CText::rebuildTextureFrom (const std::string& text) {
    // Two-pass rasterization: first measure the line, then rasterize every glyph into a single grayscale bitmap.
    // Only a single line is rendered (no wrapping).
    //
    // Safe to call repeatedly: GL handles (texture, VAO, VBO) are reused when
    // already allocated, so dynamic/scripted text can regenerate the glyph
    // bitmap every time the rendered string changes without leaking.
    FT_GlyphSlot slot = m_ftFace->glyph;
    const auto codepoints = decodeUtf8 (text);

    const int ascender = static_cast<int> (std::ceil (m_ftFace->size->metrics.ascender / 64.0));
    const int descender = static_cast<int> (std::floor (m_ftFace->size->metrics.descender / 64.0));
    const int lineHeight = std::max (1, ascender - descender);

    int penX = 0;

    for (const auto codepoint : codepoints) {
	if (FT_Load_Char (m_ftFace, codepoint, FT_LOAD_DEFAULT) != 0) {
	    continue;
	}
	penX += static_cast<int> (slot->advance.x >> 6);
    }

    int width = std::max (1, penX);

    // the measured size is what scripts see, so it's not affected by the width limit
    m_sizeValue.update (
	glm::vec2 (static_cast<float> (penX), static_cast<float> (lineHeight)),
	DynamicValue::UpdateSource::Initialization
    );

    if (m_text.limitWidth && m_text.maxWidth > 0.0f) {
	width = std::min (width, static_cast<int> (m_text.maxWidth));
    }

    const int height = lineHeight;
    std::vector<uint8_t> pixels (static_cast<size_t> (width) * height, 0);

    penX = 0;
    for (const auto codepoint : codepoints) {
	if (penX >= width) {
	    break;
	}

	if (FT_Load_Char (m_ftFace, codepoint, FT_LOAD_RENDER) != 0) {
	    continue;
	}

	const auto& bmp = slot->bitmap;
	const int originX = penX + slot->bitmap_left;
	const int originY = ascender - slot->bitmap_top;

	for (unsigned int row = 0; row < bmp.rows; ++row) {
	    for (unsigned int col = 0; col < bmp.width; ++col) {
		const int dstX = originX + static_cast<int> (col);
		const int dstY = originY + static_cast<int> (row);
		if (dstX < 0 || dstX >= width || dstY < 0 || dstY >= height) {
		    continue;
		}
		auto& pixel = pixels[static_cast<size_t> (dstY) * width + dstX];
		pixel = std::max (pixel, bmp.buffer[row * bmp.pitch + col]);
	    }
	}

	penX += static_cast<int> (slot->advance.x >> 6);
    }

    const bool firstUpload = (m_texture == 0);
    if (firstUpload) {
	glGenTextures (1, &m_texture);
    }
    glBindTexture (GL_TEXTURE_2D, m_texture);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data ());
    glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
    if (firstUpload) {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    m_textureSize = { width, height };
    m_quadSize = { static_cast<float> (width), static_cast<float> (height) };
    m_lastRenderedText = text;

    uploadQuadVertices ();
}

void CText::buildShader () {
    GLuint vs = compileShader (GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compileShader (GL_FRAGMENT_SHADER, kFragmentShader);
    if (vs == 0 || fs == 0) {
	if (vs) {
	    glDeleteShader (vs);
	}
	if (fs) {
	    glDeleteShader (fs);
	}
	return;
    }

    m_program = glCreateProgram ();
    glAttachShader (m_program, vs);
    glAttachShader (m_program, fs);
    glLinkProgram (m_program);
    glDeleteShader (vs);
    glDeleteShader (fs);

    GLint status = GL_FALSE;
    glGetProgramiv (m_program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
	char log[1024];
	glGetProgramInfoLog (m_program, sizeof (log), nullptr, log);
	sLog.error ("CText program link failed: ", log);
	glDeleteProgram (m_program);
	m_program = 0;
	return;
    }

    m_uMVP = glGetUniformLocation (m_program, "uMVP");
    m_uColor = glGetUniformLocation (m_program, "uColor");
    m_uTexture = glGetUniformLocation (m_program, "uTexture");
}

void CText::uploadQuadVertices () {
    // The quad is positioned relative to the object's origin based on the alignment: the origin is the anchor point
    // of the text (left/center/right edge horizontally, top/center/bottom edge vertically).
    // Scene-space Y grows downwards here (same convention CImage uses), so the glyph's top row (v = 0) is at y0.
    const float w = m_quadSize.x;
    const float h = m_quadSize.y;

    float x0 = -w * 0.5f;
    if (m_text.alignment == "left") {
	x0 = 0.0f;
    } else if (m_text.alignment == "right") {
	x0 = -w;
    }

    float y0 = -h * 0.5f;
    if (m_text.verticalalign == "top") {
	y0 = 0.0f;
    } else if (m_text.verticalalign == "bottom") {
	y0 = -h;
    }

    const float x1 = x0 + w;
    const float y1 = y0 + h;

    const float verts[] = {
	// pos      // uv
	x0, y0, 0.0f, 0.0f, x1, y0, 1.0f, 0.0f, x1, y1, 1.0f, 1.0f,
	x0, y0, 0.0f, 0.0f, x1, y1, 1.0f, 1.0f, x0, y1, 0.0f, 1.0f,
    };

    const bool firstUpload = (m_vao == 0);
    if (firstUpload) {
	glGenVertexArrays (1, &m_vao);
	glGenBuffers (1, &m_vbo);
    }
    glBindVertexArray (m_vao);
    glBindBuffer (GL_ARRAY_BUFFER, m_vbo);
    glBufferData (GL_ARRAY_BUFFER, sizeof (verts), verts, GL_DYNAMIC_DRAW);
    if (firstUpload) {
	glEnableVertexAttribArray (0);
	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), reinterpret_cast<void*> (0));
	glEnableVertexAttribArray (1);
	glVertexAttribPointer (
	    1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), reinterpret_cast<void*> (2 * sizeof (float))
	);
    }
    glBindVertexArray (0);
}

void CText::render () {
    if (!m_valid) {
	return;
    }

    // keep the texture (and the measured size scripts rely on) up to date even when the layer is hidden,
    // hidden text layers are commonly used to measure strings
    const std::string& renderedText = m_text.text->value->getString ();
    const unsigned int pixelSize = computePixelSize ();

    if (pixelSize != m_lastPixelSize) {
	m_lastPixelSize = pixelSize;
	FT_Set_Pixel_Sizes (m_ftFace, 0, static_cast<FT_UInt> (m_lastPixelSize));
	rebuildTextureFrom (renderedText);
    } else if (renderedText != m_lastRenderedText) {
	rebuildTextureFrom (renderedText);
    }

    if (!m_text.visible->value->getBool () || renderedText.empty ()) {
	return;
    }

    const glm::vec4 color = m_text.color->value->getVec4 ();
    const float alpha = m_text.alpha->value->getFloat () * color.a;

    if (alpha <= 0.0f) {
	return;
    }

#if !NDEBUG
    std::string str = "Text " + this->getObject ().name + " (" + std::to_string (this->getObject ().id) + ")";
    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, str.c_str ());
#endif /* DEBUG */

    const glm::vec3 scale = m_text.scale->value->getVec3 ();
    const glm::vec3 origin = m_text.origin->value->getVec3 ();

    // Scene coordinates have the origin at the bottom-left corner with Y growing upwards, convert them
    // to the centered, Y-down space the scene is rendered in (same transformation CImage applies)
    const float scene_w = getScene ().getCamera ().getWidth ();
    const float scene_h = getScene ().getCamera ().getHeight ();
    const glm::vec3 gl_origin = {
	origin.x - scene_w * 0.5f,
	scene_h * 0.5f - origin.y,
	0.0f,
    };

    glm::mat4 model = glm::translate (glm::mat4 (1.0f), gl_origin);
    model = glm::scale (model, glm::vec3 (scale.x, scale.y, 1.0f));

    const glm::mat4 mvp = getScene ().getCamera ().getProjection () * getScene ().getCamera ().getLookAt () * model;

    GLint previousVao = 0;
    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &previousVao);

    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram (m_program);
    glUniformMatrix4fv (m_uMVP, 1, GL_FALSE, glm::value_ptr (mvp));
    glUniform4f (m_uColor, color.r, color.g, color.b, alpha);

    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, m_texture);
    glUniform1i (m_uTexture, 0);

    glBindVertexArray (m_vao);
    glDrawArrays (GL_TRIANGLES, 0, 6);
    glBindVertexArray (static_cast<GLuint> (previousVao));
#if !NDEBUG
    glPopDebugGroup ();
#endif /* DEBUG */
}
