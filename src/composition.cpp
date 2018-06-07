#include "composition.h"
#include "movie_resmgr.h"

#include "simplemath.h"

extern "C" {
#include <movie/movie.h>
}


#define _GL_OFFSET(s, m) reinterpret_cast<const GLvoid*>(&(((s*)0)->m))


static const size_t kMaxVerticesToDraw  = 4 * 1024;
static const size_t kMaxIndicesToDraw   = 6 * 1024;

struct DrawVertex {
    float    pos[3];
    float    uv0[2];
    float    uv1[2];
    uint32_t color;
};

static const GLuint kVertexPosAttribIdx   = 0;
static const GLuint kVertexUV0AttribIdx   = 1;
static const GLuint kVertexUV1AttribIdx   = 2;
static const GLuint kVertexColorAttribIdx = 3;

static const GLint  kTextureRGBSlot = 0;
static const GLint  kTextureASlot   = 1;


static const char* sVertexShader = "#version 330       \n\
layout(location = 0) in vec3 inPos;                    \n\
layout(location = 1) in vec2 inUV0;                    \n\
layout(location = 2) in vec2 inUV1;                    \n\
layout(location = 3) in vec4 inColor;                  \n\
uniform mat4 uWVP;                                     \n\
out vec2 v2fUV0;                                       \n\
out vec2 v2fUV1;                                       \n\
out vec4 v2fColor;                                     \n\
void main() {                                          \n\
    gl_Position = uWVP * vec4(inPos, 1.0);             \n\
    v2fUV0 = inUV0;                                    \n\
    v2fUV1 = inUV1;                                    \n\
    v2fColor = inColor;                                \n\
}                                                      \n";

static const char* sFragmentShader = "#version 330     \n\
uniform sampler2D uTextureRGB;                         \n\
uniform sampler2D uTextureA;                           \n\
uniform bool uIsPremultAlpha;                          \n\
in vec2 v2fUV0;                                        \n\
in vec2 v2fUV1;                                        \n\
in vec4 v2fColor;                                      \n\
out vec4 oColor;                                       \n\
void main() {                                          \n\
    vec4 texColor = texture(uTextureRGB, v2fUV0);      \n\
    vec4 texAlpha = texture(uTextureA, v2fUV1);        \n\
    oColor = texColor * v2fColor;                      \n\
    if (uIsPremultAlpha) {                             \n\
        oColor *= texAlpha.a * v2fColor.a;             \n\
    } else {                                           \n\
        oColor.a *= texAlpha.a;                        \n\
    }                                                  \n\
}                                                      \n";

static const char* sWireVertexShader = "#version 330   \n\
layout(location = 0) in vec3 inPos;                    \n\
layout(location = 3) in vec4 inColor;                  \n\
uniform mat4 uWVP;                                     \n\
out vec4 v2fColor;                                     \n\
void main() {                                          \n\
    gl_Position = uWVP * vec4(inPos, 1.0);             \n\
    v2fColor = inColor;                                \n\
}                                                      \n";

static const char* sWireFragmentShader = "#version 330 \n\
in vec4 v2fColor;                                      \n\
out vec4 oColor;                                       \n\
void main() {                                          \n\
    oColor = v2fColor;                                 \n\
}                                                      \n";


static uint32_t FloatColorToUint(ae_color_t color, ae_color_channel_t alpha) {
    const uint32_t r = static_cast<uint32_t>(std::floorf(color.r * 255.5f));
    const uint32_t g = static_cast<uint32_t>(std::floorf(color.g * 255.5f));
    const uint32_t b = static_cast<uint32_t>(std::floorf(color.b * 255.5f));
    const uint32_t a = static_cast<uint32_t>(std::floorf(alpha * 255.5f));

    return (a << 24) | (b << 16) | (g << 8) | r;
}

struct TrackMatteDesc {
    float matrix[16];
    aeMovieRenderMesh mesh;
    ae_track_matte_mode_t mode;
};


Composition::Composition()
    : mComposition(nullptr)
    // rendering stuff
    , mShader(0)
    , mWireShader(0)
    , mVAO(0)
    , mVB(0)
    , mIB(0)
    , mCurrentTextureRGB(0)
    , mCurrentTextureA(0)
    , mCurrentBlendMode(BlendMode::Alpha)
    , mNumVertices(0)
    , mNumIndices(0)
    , mVerticesData(nullptr)
    , mIndicesData(nullptr)
    //
    , mDrawMode(DrawMode::Solid)
{
}

Composition::~Composition() {
    if (mComposition) {
        ae_delete_movie_composition(mComposition);
        mComposition = nullptr;
    }

    this->DestroyDrawingData();
}

std::string Composition::GetName() const {
    if (mComposition) {
        return ae_get_movie_composition_name(mComposition);
    } else {
        return std::string();
    }
}

float Composition::GetDuration() const {
    if (mComposition) {
        return ae_get_movie_composition_duration(mComposition);
    } else {
        return 0.0f;
    }
}

float Composition::GetCurrentPlayTime() const {
    if (mComposition) {
        return ae_get_movie_composition_time(mComposition);
    } else {
        return 0.0f;
    }
}

bool Composition::IsPlaying() const {
    if (mComposition) {
        return ae_is_play_movie_composition(mComposition) == AE_TRUE;
    } else {
        return false;
    }
}

void Composition::Play(const float startTime) {
    if (mComposition) {
        if(ae_is_pause_movie_composition(mComposition)) {
            ae_resume_movie_composition(mComposition);
        } else if (!this->IsPlaying()) {
            ae_play_movie_composition(mComposition, startTime);
        }
    }
}

void Composition::Pause() {
    if (mComposition) {
        ae_pause_movie_composition(mComposition);
    }
}

void Composition::Stop() {
    if (mComposition) {
        ae_stop_movie_composition(mComposition);
    }
}

void Composition::SetLoop(const bool toLoop) {
    if (mComposition) {
        ae_set_movie_composition_loop(mComposition, toLoop ? AE_TRUE : AE_FALSE);
    }
}

bool Composition::IsLooped() const {
    bool looped = false;
    if (mComposition) {
        looped = (ae_get_movie_composition_loop(mComposition) == AE_TRUE);
    }
    return looped;
}

void Composition::Update(const float deltaTime) {
    if (mComposition) {
        ae_update_movie_composition(mComposition, deltaTime);
    }
}

void Composition::Draw(const DrawMode mode) {
    static float alternativeUV[1024];

    if (mComposition) {
        mDrawMode = mode;

        this->BeginDraw();

        ae_uint32_t render_mesh_it = 0;
        aeMovieRenderMesh render_mesh;

        while (ae_compute_movie_mesh(mComposition, &render_mesh_it, &render_mesh) == AE_TRUE) {
            if (render_mesh.track_matte_data == AE_NULL) {
                switch (render_mesh.layer_type) {
                    case AE_MOVIE_LAYER_TYPE_SHAPE:
                    case AE_MOVIE_LAYER_TYPE_SOLID: {
                        if (render_mesh.vertexCount && render_mesh.indexCount) {
                            this->DrawMesh(&render_mesh, nullptr, nullptr, nullptr);
                        }

                    } break;

                    case AE_MOVIE_LAYER_TYPE_SEQUENCE:
                    case AE_MOVIE_LAYER_TYPE_IMAGE: {
                        if (render_mesh.vertexCount && render_mesh.indexCount) {
                            ResourceImage* imageRes = reinterpret_cast<ResourceImage*>(render_mesh.resource_data);
                            this->DrawMesh(&render_mesh, imageRes, nullptr, nullptr);
                        }
                    } break;
                }
            } else {
                switch (render_mesh.layer_type) {
                    case AE_MOVIE_LAYER_TYPE_SEQUENCE:
                    case AE_MOVIE_LAYER_TYPE_IMAGE: {
                        if (render_mesh.element_data && render_mesh.vertexCount) {
                            const TrackMatteDesc* track_matte_desc = reinterpret_cast<const TrackMatteDesc*>(render_mesh.track_matte_data);
                            const aeMovieRenderMesh& track_matte_mesh = track_matte_desc->mesh;

                            ResourceImage* matteImageRes = reinterpret_cast<ResourceImage*>(render_mesh.element_data);
                            ResourceImage* imageRes = reinterpret_cast<ResourceImage*>(render_mesh.resource_data);

                            for (ae_uint32_t i = 0; i != track_matte_mesh.vertexCount; ++i) {
                                const float* mesh_position = track_matte_mesh.position[i];

                                CalcPointUV(&alternativeUV[i * 2],
                                            render_mesh.position[0],
                                            render_mesh.position[1],
                                            render_mesh.position[2],
                                            render_mesh.uv[0],
                                            render_mesh.uv[1],
                                            render_mesh.uv[2],
                                            mesh_position);
                            }

                            this->DrawMesh(&track_matte_mesh, matteImageRes, imageRes, alternativeUV);
                        }

                    } break;
                }
            }
        }

        this->EndDraw();
    }
}

size_t Composition::GetNumSubCompositions() const {
    return mSubCompositions.size();
}

const char* Composition::GetSubCompositionName(const size_t idx) const {
    return ae_get_movie_sub_composition_name(mSubCompositions[idx]);
}

void Composition::PlaySubComposition(const size_t idx) {
    if (mComposition && idx < mSubCompositions.size()) {
        const aeMovieSubComposition* subComposition = mSubCompositions[idx];
        if (ae_is_pause_movie_sub_composition(subComposition)) {
            ae_resume_movie_sub_composition(mComposition, subComposition);
        } else {
            ae_play_movie_sub_composition(mComposition, subComposition, 0);
        }
    }
}

void Composition::PauseSubComposition(const size_t idx) {
    if (mComposition && idx < mSubCompositions.size()) {
        ae_pause_movie_sub_composition(mComposition, mSubCompositions[idx]);
    }
}

void Composition::StopSubComposition(const size_t idx) {
    if (mComposition && idx < mSubCompositions.size()) {
        ae_stop_movie_sub_composition(mComposition, mSubCompositions[idx]);
    }
}

void Composition::SetTimeSubComposition(const size_t idx, const float time) {
    if (mComposition && idx < mSubCompositions.size()) {
        ae_set_movie_sub_composition_time(mComposition, mSubCompositions[idx], static_cast<ae_time_t>(time));
    }
}

void Composition::SetLoopSubComposition(const size_t idx, const bool toLoop) {
    if (mComposition && idx < mSubCompositions.size()) {
        ae_set_movie_sub_composition_loop(mSubCompositions[idx], toLoop ? AE_TRUE : AE_FALSE);
    }
}

bool Composition::IsLoopedSubComposition(const size_t idx) const {
    bool looped = false;
    if (mComposition && idx < mSubCompositions.size()) {
        looped = (ae_get_movie_sub_composition_loop(mSubCompositions[idx]) == AE_TRUE);
    }
    return looped;
}

void Composition::Create(const aeMovieData* moviewData, const aeMovieCompositionData* compData) {
    aeMovieCompositionProviders providers;
    ae_clear_movie_composition_providers(&providers);

    providers.node_provider = [](const aeMovieNodeProviderCallbackData* _callbackData, ae_voidptrptr_t _nd, ae_voidptr_t _ud)->ae_bool_t {
        Composition* _this = reinterpret_cast<Composition*>(_ud);
        if (_this) {
            return _this->OnProvideNode(_callbackData, _nd) ? AE_TRUE : AE_FALSE;
        } else {
            return AE_FALSE;
        }
    };

    providers.node_deleter = [](const aeMovieNodeDeleterCallbackData* _callbackData, ae_voidptr_t _ud) {
        Composition* _this = reinterpret_cast<Composition*>(_ud);
        if (_this) {
            _this->OnDeleteNode(_callbackData);
        }
    };

    providers.node_update = [](const aeMovieNodeUpdateCallbackData* _callbackData, ae_voidptr_t _ud) {
        Composition* _this = reinterpret_cast<Composition*>(_ud);
        if (_this) {
            _this->OnUpdateNode(_callbackData);
        }
    };

    providers.camera_provider = [](const aeMovieCameraProviderCallbackData* _callbackData, ae_voidptrptr_t _cd, ae_voidptr_t _ud)->ae_bool_t {
        Composition* _this = reinterpret_cast<Composition*>(_ud);
        if (_this) {
            return _this->OnProvideCamera(_callbackData, _cd) ? AE_TRUE : AE_FALSE;
        } else {
            return AE_FALSE;
        }
    };

    providers.track_matte_provider = [](const aeMovieTrackMatteProviderCallbackData * _callbackData, ae_voidptrptr_t _tmd, ae_voidptr_t _ud)->ae_bool_t {
        Composition* _this = reinterpret_cast<Composition*>(_ud);
        if (_this) {
            return _this->OnProvideTrackMatte(_callbackData, _tmd) ? AE_TRUE : AE_FALSE;
        } else {
            return AE_FALSE;
        }
    };

    providers.track_matte_update = [](const aeMovieTrackMatteUpdateCallbackData* _callbackData, ae_voidptr_t _ud) {
        Composition* _this = reinterpret_cast<Composition*>(_ud);
        if (_this) {
            _this->OnUpdateTrackMatte(_callbackData);
        }
    };

    providers.track_matte_deleter = [](const aeMovieTrackMatteDeleterCallbackData * _callbackData, ae_voidptr_t _ud) {
        Composition* _this = reinterpret_cast<Composition*>(_ud);
        if (_this) {
            _this->OnDeleteTrackMatte(_callbackData);
        }
    };

    providers.composition_event = [](const aeMovieCompositionEventCallbackData* _callbackData, ae_voidptr_t _ud) {
        Composition* _this = reinterpret_cast<Composition*>(_ud);
        if (_this) {
            _this->OnCompositionEffect(_callbackData);
        }
    };

    providers.composition_state = [](const aeMovieCompositionStateCallbackData* _callbackData, ae_voidptr_t _ud) {
        Composition* _this = reinterpret_cast<Composition*>(_ud);
        if (_this) {
            _this->OnCompositionState(_callbackData);
        }
    };

    mComposition = ae_create_movie_composition(moviewData, compData, AE_TRUE, &providers, this);

    if (mComposition) {
        ae_visit_movie_sub_composition(mComposition, [](const aeMovieComposition*, ae_uint32_t, const ae_char_t*, const aeMovieSubComposition* _subcomposition, ae_voidptr_t _ud)->ae_bool_t {
            Composition* _this = reinterpret_cast<Composition*>(_ud);
            if (_this) {
                _this->AddSubComposition(_subcomposition);
            }
            return AE_TRUE;
        }, this);
    }

    this->CreateDrawingData();
}

void Composition::AddSubComposition(const aeMovieSubComposition* subComposition) {
    mSubCompositions.push_back(subComposition);
}


static GLuint CompileShader(const char* src, const GLenum type, std::string& log) {
    GLuint shader = glCreateShader(type);
    if (shader) {
        GLint status = 0;
        glShaderSource(shader, 1, &src, 0);
        glCompileShader(shader);
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

        // we grab the log anyway
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            log.resize(infoLen);
            glGetShaderInfoLog(shader, infoLen, nullptr, const_cast<GLchar*>(log.data())); // non-const .data() in c++17
        }

        if (!status) {
            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}

static GLuint CreateShader(const char* vs, const char* fs) {
    std::string vsLog, fsLog;
    GLuint vertexShader = CompileShader(vs, GL_VERTEX_SHADER, vsLog);
    GLuint fragmentShader = CompileShader(fs, GL_FRAGMENT_SHADER, fsLog);

    GLuint shader = 0;

    if (vertexShader && fragmentShader) {
        shader = glCreateProgram();
        if (shader) {
            glAttachShader(shader, vertexShader);
            glAttachShader(shader, fragmentShader);
            glLinkProgram(shader);

            // we don't need our shader objects anymore
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);

            // grab the log
            GLint infoLen = 0;
            glGetProgramiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen > 1) {
                std::string log; log.resize(infoLen);
                glGetProgramInfoLog(shader, infoLen, nullptr, const_cast<GLchar*>(log.data())); // non-const .data() in c++17

                MyLog << "Shader link:" << MyEndl << log << MyEndl;
            }

            GLint status = 0;
            glGetProgramiv(shader, GL_LINK_STATUS, &status);
            if (!status) {
                glDeleteProgram(shader);
            } else {
                glUseProgram(shader);
            }
        }
    } else {
        if (!vsLog.empty()) {
            MyLog <<"Vertex shader compilation:" << MyEndl << vsLog << MyEndl;
        }
        if (!fsLog.empty()) {
            MyLog << "Fragment shader compilation:" << MyEndl << fsLog << MyEndl;
        }
    }

    return shader;
}

void Composition::CreateDrawingData() {
    const GLsizeiptr vbSize = static_cast<GLsizeiptr>(kMaxVerticesToDraw * sizeof(DrawVertex));
    const GLsizeiptr ibSize = static_cast<GLsizeiptr>(kMaxIndicesToDraw * sizeof(uint16_t));

    // create shader program
    mShader = CreateShader(sVertexShader, sFragmentShader);
    mWireShader = CreateShader(sWireVertexShader, sWireFragmentShader);

    const aeMovieCompositionData* data = ae_get_movie_composition_composition_data(mComposition);

    const float left = 0.0f;
    const float right = ae_get_movie_composition_data_width(data);
    const float bottom = ae_get_movie_composition_data_height(data);
    const float top = 0.0f;
    const float zNear = -1.0f;
    const float zFar = 1.0f;

    float projOrtho[16];
    MakeOrtho2DMat(left, right, top, bottom, zNear, zFar, projOrtho);

    glUseProgram(mShader);
    mIsPremultAlphaUniform = glGetUniformLocation(mShader, "uIsPremultAlpha");
    if (mIsPremultAlphaUniform >= 0) {
        glUniform1i(mIsPremultAlphaUniform, GL_FALSE);
    }

    GLint mvpLoc = glGetUniformLocation(mShader, "uWVP");
    if (mvpLoc >= 0) {
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, projOrtho);
    }

    GLint texLocRGB = glGetUniformLocation(mShader, "uTextureRGB");
    if (texLocRGB >= 0) {
        glUniform1i(texLocRGB, kTextureRGBSlot);
    }

    GLint texLocA = glGetUniformLocation(mShader, "uTextureA");
    if (texLocA >= 0) {
        glUniform1i(texLocA, kTextureASlot);
    }

    glUseProgram(mWireShader);
    mvpLoc = glGetUniformLocation(mWireShader, "uWVP");
    if (mvpLoc >= 0) {
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, projOrtho);
    }

    // create vertex buffer
    glGenBuffers(1, &mVB);
    glBindBuffer(GL_ARRAY_BUFFER, mVB);
    glBufferData(GL_ARRAY_BUFFER, vbSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // create index buffer
    glGenBuffers(1, &mIB);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIB);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ibSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // create vao to hold vertex attribs bindings
    glGenVertexArrays(1, &mVAO);
    glBindVertexArray(mVAO);

    // attach vb
    glBindBuffer(GL_ARRAY_BUFFER, mVB);

    // enable our attributes
    glEnableVertexAttribArray(kVertexPosAttribIdx);
    glEnableVertexAttribArray(kVertexUV0AttribIdx);
    glEnableVertexAttribArray(kVertexUV1AttribIdx);
    glEnableVertexAttribArray(kVertexColorAttribIdx);

    // bind our attributes
    const GLsizei stride = static_cast<GLsizei>(sizeof(DrawVertex));
    glVertexAttribPointer(kVertexPosAttribIdx,   3, GL_FLOAT,         GL_FALSE, stride, _GL_OFFSET(DrawVertex, pos));
    glVertexAttribPointer(kVertexUV0AttribIdx,   2, GL_FLOAT,         GL_FALSE, stride, _GL_OFFSET(DrawVertex, uv0));
    glVertexAttribPointer(kVertexUV1AttribIdx,   2, GL_FLOAT,         GL_FALSE, stride, _GL_OFFSET(DrawVertex, uv1));
    glVertexAttribPointer(kVertexColorAttribIdx, 4, GL_UNSIGNED_BYTE, GL_TRUE,  stride, _GL_OFFSET(DrawVertex, color));

    // attach ib
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIB);
}

void Composition::DestroyDrawingData() {
    glBindVertexArray(mVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mVB);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIB);
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

    glDeleteBuffers(1, &mVB);
    glDeleteBuffers(1, &mIB);
    glDeleteVertexArrays(1, &mVAO);

    glDeleteShader(mShader);
    glDeleteShader(mWireShader);
}

void Composition::BeginDraw() {
    glBindVertexArray(mVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mVB);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIB);
    mVerticesData = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    mIndicesData = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
}

void Composition::EndDraw() {
    this->FlushDraw();

    glUnmapBuffer(GL_ARRAY_BUFFER);
    glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
}

void Composition::DrawMesh(const aeMovieRenderMesh* mesh, const ResourceImage* imageRGB, const ResourceImage* imageA, const float* alternativeUV) {
    const size_t verticesLeft = kMaxVerticesToDraw - mNumVertices;
    const size_t indicesLeft = kMaxIndicesToDraw - mNumIndices;

    GLuint newTextureRGB = ResourcesManager::Instance().GetWhiteTexture();
    if (imageRGB != nullptr && imageRGB->textureRes != nullptr) {
        newTextureRGB = imageRGB->textureRes->texture;
    }

    GLuint newTextureA = ResourcesManager::Instance().GetWhiteTexture();
    if (imageA != nullptr && imageA->textureRes != nullptr) {
        newTextureA = imageA->textureRes->texture;
    }

    BlendMode newBlendMode = BlendMode::Alpha;
    if (imageRGB && imageRGB->premultAlpha) {
        newBlendMode = BlendMode::PremultAlpha;
    }

    if (mesh->blend_mode == AE_MOVIE_BLEND_ADD) {
        newBlendMode = BlendMode::Add;
    }

    if (mesh->vertexCount > verticesLeft    ||
        mesh->indexCount > indicesLeft      ||
        newTextureRGB != mCurrentTextureRGB ||
        newTextureA != mCurrentTextureA     ||
        newBlendMode != mCurrentBlendMode) {
        this->FlushDraw();
    }

    mCurrentTextureRGB = newTextureRGB;
    mCurrentTextureA = newTextureA;
    mCurrentBlendMode = newBlendMode;

    DrawVertex* vertices = reinterpret_cast<DrawVertex*>(mVerticesData) + mNumVertices;
    uint16_t* indices = reinterpret_cast<uint16_t*>(mIndicesData) + mNumIndices;

    for (size_t i = 0; i < mesh->vertexCount; ++i, ++vertices) {
        vertices->pos[0] = mesh->position[i][0];
        vertices->pos[1] = mesh->position[i][1];
        vertices->pos[2] = mesh->position[i][2];

        if (alternativeUV) {
            vertices->uv0[0] = alternativeUV[i * 2 + 0];
            vertices->uv0[1] = alternativeUV[i * 2 + 1];
            vertices->uv1[0] = mesh->uv[i][0];
            vertices->uv1[1] = mesh->uv[i][1];
        } else {
            vertices->uv1[0] = vertices->uv0[0] = mesh->uv[i][0];
            vertices->uv1[1] = vertices->uv0[1] = mesh->uv[i][1];
        }

        vertices->color = FloatColorToUint(mesh->color, mesh->opacity);
    }

    for (size_t i = 0; i < mesh->indexCount; ++i) {
        indices[i] = static_cast<uint16_t>((mesh->indices[i] + mNumVertices) & 0xffff);
    }

    mNumVertices += mesh->vertexCount;
    mNumIndices += mesh->indexCount;

    this->FlushDraw();
}

void Composition::FlushDraw() {
    if (mNumIndices) {
        const bool drawSolid = (mDrawMode == DrawMode::Solid || mDrawMode == DrawMode::SolidWithWireOverlay);
        const bool drawWire = (mDrawMode == DrawMode::Wireframe || mDrawMode == DrawMode::SolidWithWireOverlay);

        GLint isPremultAlpha = GL_FALSE;

        switch (mCurrentBlendMode) {
            case BlendMode::Alpha: {
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            } break;

            case BlendMode::PremultAlpha: {
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                isPremultAlpha = GL_TRUE;
            } break;

            case BlendMode::Add: {
                glBlendFunc(GL_ONE, GL_ONE);
            } break;
        }

        glUnmapBuffer(GL_ARRAY_BUFFER);
        glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

        glActiveTexture(GL_TEXTURE0 + kTextureRGBSlot);
        glBindTexture(GL_TEXTURE_2D, mCurrentTextureRGB);
        glActiveTexture(GL_TEXTURE0 + kTextureASlot);
        glBindTexture(GL_TEXTURE_2D, mCurrentTextureA);

        if (drawSolid) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glUseProgram(mShader);
            glUniform1i(mIsPremultAlphaUniform, isPremultAlpha);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mNumIndices), GL_UNSIGNED_SHORT, nullptr);
        }

        if (drawWire) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glUseProgram(mWireShader);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mNumIndices), GL_UNSIGNED_SHORT, nullptr);
        }

        mVerticesData = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
        mIndicesData = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
    }

    mNumVertices = 0;
    mNumIndices = 0;
}


// callbacks
bool Composition::OnProvideNode(const aeMovieNodeProviderCallbackData* _callbackData, void** _nd) {
    AE_UNUSED(_nd);

    MyLog << "Node provider callback" << MyEndl;

    if (ae_is_movie_layer_data_track_mate(_callbackData->layer) == AE_TRUE) {
        MyLog << " Is track matte layer" << MyEndl;
        return true;
    }

    aeMovieLayerTypeEnum layerType = ae_get_movie_layer_data_type(_callbackData->layer);

    MyLog << " Layer: '" << ae_get_movie_layer_data_name(_callbackData->layer) << MyEndl;

    if (_callbackData->track_matte_layer == nullptr) {
        MyLog << " Has track matte: no" << MyEndl;
        MyLog << " Type:";

        switch (layerType) {
            case AE_MOVIE_LAYER_TYPE_SLOT:  MyLog << " slot"  << MyEndl; break;
            case AE_MOVIE_LAYER_TYPE_VIDEO: MyLog << " video" << MyEndl; break;
            case AE_MOVIE_LAYER_TYPE_SOUND: MyLog << " sound" << MyEndl; break;
            case AE_MOVIE_LAYER_TYPE_IMAGE: MyLog << " image" << MyEndl; break;
            default:
                MyLog << " other" << MyEndl;
                break;
        }
    } else {
        MyLog << " Has track matte: yes" << MyEndl;
        MyLog << " Type:";

        switch (layerType) {
            case AE_MOVIE_LAYER_TYPE_SHAPE: MyLog << " shape" << MyEndl; break;
            case AE_MOVIE_LAYER_TYPE_IMAGE: {
                MyLog << " image" << MyEndl;

                ResourceImage* resourceTrackMatteImage = reinterpret_cast<ResourceImage*>(ae_get_movie_layer_data_resource_data(_callbackData->track_matte_layer));
                *_nd = reinterpret_cast<ae_voidptr_t>(resourceTrackMatteImage);
            } break;
            default:
                MyLog << " other" << MyEndl;
                break;
        }
    }

    return true;
}

void Composition::OnDeleteNode(const aeMovieNodeDeleterCallbackData* _callbackData) {
    MyLog << "Node destroyer callback." << MyEndl;
    aeMovieLayerTypeEnum layerType = ae_get_movie_layer_data_type(_callbackData->layer);
    MyLog << " Layer type: " << layerType << MyEndl;
}

void Composition::OnUpdateNode(const aeMovieNodeUpdateCallbackData* _callbackData) {
    AE_UNUSED(_callbackData);
}

bool Composition::OnProvideCamera(const aeMovieCameraProviderCallbackData* _callbackData, void** _cd) {
    AE_UNUSED(_callbackData);
    AE_UNUSED(_cd);

    MyLog << "Camera provider callback." << MyEndl;

    return true;
}

bool Composition::OnProvideTrackMatte(const aeMovieTrackMatteProviderCallbackData * _callbackData, void** _tmd) {
    TrackMatteDesc* desc = new TrackMatteDesc();
    memcpy(desc->matrix, _callbackData->matrix, sizeof(desc->matrix));
    desc->mesh = _callbackData->mesh[0];
    desc->mode = _callbackData->track_matte_mode;

    *_tmd = desc;

    return true;
}

void Composition::OnUpdateTrackMatte(const aeMovieTrackMatteUpdateCallbackData* _callbackData) {
    switch (_callbackData->state) {
        case AE_MOVIE_STATE_UPDATE_BEGIN: {
            TrackMatteDesc* desc = reinterpret_cast<TrackMatteDesc *>(_callbackData->track_matte_data);
            if (desc) {
                memcpy(desc->matrix, _callbackData->matrix, sizeof(desc->matrix));
                desc->mesh = _callbackData->mesh[0];
            }
        } break;

        case AE_MOVIE_STATE_UPDATE_PROCESS: {
            TrackMatteDesc* desc = reinterpret_cast<TrackMatteDesc *>(_callbackData->track_matte_data);
            if (desc) {
                memcpy(desc->matrix, _callbackData->matrix, sizeof(desc->matrix));
                desc->mesh = _callbackData->mesh[0];
            }
        } break;
    }
}

void Composition::OnDeleteTrackMatte(const aeMovieTrackMatteDeleterCallbackData* _callbackData) {
    TrackMatteDesc* desc = reinterpret_cast<TrackMatteDesc *>(_callbackData->track_matte_data);
    if (desc) {
        delete desc;
    }
}

void Composition::OnCompositionEffect(const aeMovieCompositionEventCallbackData* _callbackData) {
    MyLog << "Composition event callback." << MyEndl;
}

void Composition::OnCompositionState(const aeMovieCompositionStateCallbackData* _callbackData) {
    AE_UNUSED(_callbackData);
}
