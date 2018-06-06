#pragma once
#include <glad/glad.h>
#include <string>
#include <unordered_map>

#include "singleton.h"


struct Resource {
    enum : size_t {
        Solid = 0,
        Video,
        Sound,
        Image,
        Sequence,
        Particle,
        Slot,

        // System
        Texture = 1000
    };

    size_t  type;
};

struct ResourceTexture : public Resource {
    enum : size_t {
        R8 = 0,
        R8G8,
        R8G8B8,
        R8G8B8A8
    };

    size_t  width;
    size_t  height;
    size_t  format;
    GLuint  texture;

    ResourceTexture() {
        type = Resource::Texture;
    }
};

struct ResourceImage : public Resource {
    ResourceTexture*    textureRes;
    bool                premultAlpha;

    ResourceImage()
        : textureRes(nullptr)
        , premultAlpha(true)
    {
        type = Resource::Image;
    }
};

DECLARE_SINGLETON(ResourcesManager) {
public:
    ResourcesManager();
    ~ResourcesManager();

    void                Initialize();
    void                Shutdown();

    GLuint              GetWhiteTexture() const;

    ResourceTexture*    GetTextureRes(const std::string& fileName);
    ResourceImage*      GetImageRes(const std::string& imageName);

private:
    ResourceTexture*    LoadTextureRes(const std::string& fileName);

private:
    typedef std::unordered_map<size_t, Resource*> ResourcesTable;

    GLuint          mWhiteTexture;
    ResourcesTable  mResources;
};
