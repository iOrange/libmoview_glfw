#include "movie_resmgr.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>



static size_t FNV1A_Hash(const std::string& str) {
    size_t result = 0;
    const char* traitName = str.c_str();
    const size_t len = str.length();
    size_t mult = 1u, c = 816753u;
    for (size_t i = 0; i < len; ++i) {
        result += static_cast<size_t>(traitName[i]) * mult;
        mult += c;
        c += (c + 13u) >> 1u;
    }
    return result;
}



ResourcesManager::ResourcesManager()
    : mWhiteTexture(0)
{
}
ResourcesManager::~ResourcesManager() {
}

void ResourcesManager::Initialize() {
    if (!mWhiteTexture) {
        uint32_t whitePixel = 0xFFFFFFFF;

        glGenTextures(1, &mWhiteTexture);
        glBindTexture(GL_TEXTURE_2D, mWhiteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whitePixel);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void ResourcesManager::Shutdown() {
    if (mWhiteTexture) {
        glDeleteTextures(1, &mWhiteTexture);
        mWhiteTexture = 0;
    }

    for (auto& p : mResources) {
        Resource* res = p.second;
        if (res->type == Resource::Texture) {
            ResourceTexture* tex = static_cast<ResourceTexture*>(res);
            glDeleteTextures(1, &tex->texture);
        }

        delete res;
    }

    mResources.clear();
}

GLuint ResourcesManager::GetWhiteTexture() const {
    return mWhiteTexture;
}

ResourceTexture* ResourcesManager::GetTextureRes(const std::string& fileName) {
    const size_t hash = FNV1A_Hash(fileName);
    ResourcesTable::iterator it = mResources.find(hash);
    if (it != mResources.end() && it->second->type == Resource::Texture) {
        return static_cast<ResourceTexture*>(it->second);
    } else {
        return this->LoadTextureRes(fileName);
    }
}

ResourceImage* ResourcesManager::GetImageRes(const std::string& imageName) {
    const size_t hash = FNV1A_Hash(imageName);
    ResourcesTable::iterator it = mResources.find(hash);
    if (it != mResources.end() && it->second->type == Resource::Image) {
        return static_cast<ResourceImage*>(it->second);
    } else {
        ResourceImage* image = new ResourceImage();
        mResources.insert({hash, image});
        return image;
    }
}

ResourceTexture* ResourcesManager::LoadTextureRes(const std::string& fileName) {
    int width, height, comp;
    uint8_t* data = stbi_load(fileName.c_str(), &width, &height, &comp, STBI_default);

    ResourceTexture* texture = nullptr;
    if (data) {
        texture = new ResourceTexture();
        texture->width = static_cast<size_t>(width);
        texture->height = static_cast<size_t>(height);

        GLint internalFmt;
        GLenum format;
        switch (comp) {
            case 1: {
                texture->format = ResourceTexture::R8;
                internalFmt = GL_R8;
                format = GL_RED;
            } break;
            case 2: {
                texture->format = ResourceTexture::R8G8;
                internalFmt = GL_RG8;
                format = GL_RG;
            } break;
            case 3: {
                texture->format = ResourceTexture::R8G8B8;
                internalFmt = GL_RGB8;
                format = GL_RGB;
            } break;
            case 4: {
                texture->format = ResourceTexture::R8G8B8A8;
                internalFmt = GL_RGBA8;
                format = GL_RGBA;
            } break;
        }

        glGenTextures(1, &texture->texture);
        glBindTexture(GL_TEXTURE_2D, texture->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0, format, GL_UNSIGNED_BYTE, data);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glBindTexture(GL_TEXTURE_2D, 0);

        stbi_image_free(data);

        const size_t hash = FNV1A_Hash(fileName);
        mResources.insert({hash, texture});
    }

    return texture;
}
