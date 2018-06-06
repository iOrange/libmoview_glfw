#pragma once
#include "utils.h"
#include <glad/glad.h>

struct aeMovieData;
struct aeMovieCompositionData;
struct aeMovieComposition;
struct aeMovieSubComposition;
struct aeMovieRenderMesh;
struct aeMovieNodeProviderCallbackData;
struct aeMovieNodeDeleterCallbackData;
struct aeMovieNodeUpdateCallbackData;
struct aeMovieCameraProviderCallbackData;
struct aeMovieTrackMatteProviderCallbackData;
struct aeMovieTrackMatteUpdateCallbackData;
struct aeMovieTrackMatteDeleterCallbackData;
struct aeMovieCompositionEventCallbackData;
struct aeMovieCompositionStateCallbackData;


struct ResourceImage;

class Composition {
    friend class Movie;

public:
    enum class DrawMode : size_t {
        Solid,
        Wireframe,
        SolidWithWireOverlay
    };

    enum class BlendMode : size_t {
        Alpha,
        PremultAlpha,
        Add
    };

protected:
    Composition();

public:
    ~Composition();

    std::string GetName() const;
    float       GetDuration() const;
    float       GetCurrentPlayTime() const;

    bool        IsPlaying() const;
    void        Play(const float startTime = 0.0f);
    void        Pause();
    void        Stop();
    void        SetLoop(const bool toLoop);
    bool        IsLooped() const;

    void        Update(const float deltaTime);
    void        Draw(const DrawMode mode);

    // sub compositions
    size_t      GetNumSubCompositions() const;
    const char* GetSubCompositionName(const size_t idx) const;
    void        PlaySubComposition(const size_t idx);
    void        PauseSubComposition(const size_t idx);
    void        StopSubComposition(const size_t idx);
    void        SetTimeSubComposition(const size_t idx, const float time);
    void        SetLoopSubComposition(const size_t idx, const bool toLoop);
    bool        IsLoopedSubComposition(const size_t idx) const;

protected:
    void        Create(const aeMovieData* moviewData, const aeMovieCompositionData* compData);
    void        AddSubComposition(const aeMovieSubComposition* subComposition);
    void        CreateDrawingData();
    void        DestroyDrawingData();

    void        BeginDraw();
    void        EndDraw();
    void        DrawMesh(const aeMovieRenderMesh* mesh, const ResourceImage* imageRGB, const ResourceImage* imageA, const float* alternativeUV);
    void        FlushDraw();

    bool        OnProvideNode(const aeMovieNodeProviderCallbackData* _callbackData, void** _nd);
    void        OnDeleteNode(const aeMovieNodeDeleterCallbackData* _callbackData);
    void        OnUpdateNode(const aeMovieNodeUpdateCallbackData* _callbackData);
    bool        OnProvideCamera(const aeMovieCameraProviderCallbackData* _callbackData, void** _cd);
    bool        OnProvideTrackMatte(const aeMovieTrackMatteProviderCallbackData * _callbackData, void** _tmd);
    void        OnUpdateTrackMatte(const aeMovieTrackMatteUpdateCallbackData* _callbackData);
    void        OnDeleteTrackMatte(const aeMovieTrackMatteDeleterCallbackData* _callbackData);
    void        OnCompositionEffect(const aeMovieCompositionEventCallbackData* _callbackData);
    void        OnCompositionState(const aeMovieCompositionStateCallbackData* _callbackData);

private:
    const aeMovieComposition*                   mComposition;
    std::vector<const aeMovieSubComposition*>   mSubCompositions;

    // rendering stuff
    GLuint                                      mShader;
    GLuint                                      mWireShader;
    GLint                                       mIsPremultAlphaUniform;
    GLuint                                      mVAO;
    GLuint                                      mVB;
    GLuint                                      mIB;
    GLuint                                      mCurrentTextureRGB;
    GLuint                                      mCurrentTextureA;
    BlendMode                                   mCurrentBlendMode;
    size_t                                      mNumVertices;
    size_t                                      mNumIndices;
    void*                                       mVerticesData;
    void*                                       mIndicesData;

    DrawMode                                    mDrawMode;
};
