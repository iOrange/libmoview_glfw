#include "movie.h"

#include <cstdarg>

extern "C" {
#include <movie/movie.h>
}

#include "movie_resmgr.h"
#include "composition.h"

// example callbacks, replace with your own
AE_CALLBACK ae_voidptr_t my_alloc(ae_voidptr_t, ae_size_t _size) {
    return malloc(_size);
}

AE_CALLBACK ae_voidptr_t my_alloc_n(ae_voidptr_t, ae_size_t _size, ae_size_t _count) {
    ae_size_t total = _size * _count;
    return malloc(total);
}

AE_CALLBACK ae_void_t my_free(ae_voidptr_t, ae_constvoidptr_t _ptr) {
    free((ae_voidptr_t)_ptr);
}

AE_CALLBACK ae_void_t my_free_n(ae_voidptr_t, ae_constvoidptr_t _ptr) {
    free((ae_voidptr_t)_ptr);
}

AE_CALLBACK ae_int32_t my_strncmp(ae_voidptr_t, const ae_char_t * _src, const ae_char_t * _dst, ae_size_t _count) {
    return (ae_int32_t)strncmp(_src, _dst, _count);
}

AE_CALLBACK ae_void_t my_logerror(ae_voidptr_t, aeMovieErrorCode, const ae_char_t* _format, ...) {
    va_list argList;
    va_start(argList, _format);
    vprintf(_format, argList);
    va_end(argList);
}

// example i/o functionality, replace with your own
struct MemoryIO {
    const uint8_t*  data;
    size_t          dataLength;
    size_t          cursor;
};

AE_CALLBACK ae_size_t my_io_read(ae_voidptr_t _data, ae_voidptr_t _buff, ae_size_t, ae_size_t _size) {
    MemoryIO* io = reinterpret_cast<MemoryIO*>(_data);
    if (io && (io->cursor < io->dataLength)) {
        const size_t dataAvailable = io->dataLength - io->cursor;
        const size_t toRead = (_size > dataAvailable) ? dataAvailable : _size;
        memcpy(_buff, io->data + io->cursor, toRead);
        io->cursor += toRead;
        return toRead;
    } else {
        return 0;
    }
}

AE_CALLBACK ae_void_t my_memory_copy(ae_voidptr_t, ae_constvoidptr_t _src, ae_voidptr_t _dst, ae_size_t _size) {
    memcpy(_dst, _src, _size);
}


AE_CALLBACK ae_bool_t my_resource_provider(const aeMovieResource * _resource, ae_voidptrptr_t _rd, ae_voidptr_t _ud) {
    
}

AE_CALLBACK ae_void_t my_resource_deleter(aeMovieResourceTypeEnum _type, ae_voidptr_t _data, ae_voidptr_t _ud) {
}


Movie::Movie()
    : mMovieInstance(nullptr)
    , mMovieData(nullptr)
    , mVersion(0.0f)
{
}
Movie::~Movie() {
    this->Close();
}

bool Movie::LoadFromFile(const std::string& fileName, const std::string& licenseHash) {
    bool result = false;

    FILE* f = my_fopen(fileName.c_str(), "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        const size_t fileLen = static_cast<size_t>(ftell(f));
        fseek(f, 0, SEEK_SET);

        std::vector<uint8_t> buffer(fileLen);
        fread(buffer.data(), 1, fileLen, f);
        fclose(f);

        std::string baseFolder;

        // looking for last delimiter to extract base folder (we try both forward and backward slashes)
        size_t lastDelimiter = fileName.find_last_of('\\');
        if (lastDelimiter == std::string::npos) {
            lastDelimiter = fileName.find_last_of('/');
        }

        if (lastDelimiter != std::string::npos) {
            baseFolder = fileName.substr(0, lastDelimiter + 1);
        }

        result = this->LoadFromMemory(buffer.data(), fileLen, baseFolder, licenseHash);
    }

    return result;
}

bool Movie::LoadFromMemory(const void* data, const size_t dataLength, const std::string& baseFolder, const std::string& licenseHash) {
    bool result = false;

    this->Close();

    const aeMovieInstance* movie = ae_create_movie_instance(licenseHash.c_str(),
                                                            &my_alloc,
                                                            &my_alloc_n,
                                                            &my_free,
                                                            &my_free_n,
                                                            &my_strncmp,
                                                            &my_logerror,
                                                            nullptr);

    if (movie) {
        MemoryIO io;
        io.data = reinterpret_cast<const uint8_t*>(data);
        io.dataLength = dataLength;
        io.cursor = 0;

        aeMovieStream* stream = ae_create_movie_stream(movie, &my_io_read, &my_memory_copy, &io);

        aeMovieDataProviders dataProviders;
        ae_clear_movie_data_providers(&dataProviders);
        dataProviders.resource_provider = [](const aeMovieResource * _resource, ae_voidptrptr_t _rd, ae_voidptr_t _ud)->ae_bool_t {
            Movie* _this = reinterpret_cast<Movie*>(_ud);
            if (_this) {
                return _this->OnProvideResource(_resource, _rd, _ud) ? AE_TRUE : AE_FALSE;
            } else {
                return false;
            }
        };
        dataProviders.resource_deleter = [](aeMovieResourceTypeEnum _type, ae_voidptr_t _data, ae_voidptr_t _ud) {
            Movie* _this = reinterpret_cast<Movie*>(_ud);
            if (_this) {
                _this->OnDeleteResource(_type, _data, _ud);
            }
        };
        aeMovieData* data = ae_create_movie_data(movie, &dataProviders, this);

        // save the base folder, we'll need it later to look for resources
        mBaseFolder = baseFolder;

        ae_uint32_t major_version;
        ae_uint32_t minor_version;
        ae_result_t movie_data_result = ae_load_movie_data(data, stream, &major_version, &minor_version);
        if (movie_data_result != AE_RESULT_SUCCESSFUL) {
            ae_delete_movie_data(data);
            ae_delete_movie_stream(stream);
            ae_delete_movie_instance(movie);
        } else {
            // now we can free the stream as all the data is now loaded
            ae_delete_movie_stream(stream);
            mMovieInstance = movie;
            mMovieData = data;
            mVersion = static_cast<float>(major_version) + (static_cast<float>(minor_version) * 0.1f);

            result = true;
        }
    }

    return result;
}

void Movie::Close() {
    if (mMovieData) {
        ae_delete_movie_data(mMovieData);
        mMovieData = nullptr;
    }

    if (mMovieInstance) {
        ae_delete_movie_instance(mMovieInstance);
        mMovieInstance = nullptr;
    }
}

float Movie::GetVersion() const {
    return mVersion;
}

Composition* Movie::OpenComposition(const std::string& name) {
    Composition* result = nullptr;

    if (mMovieData) {
        const aeMovieCompositionData* compData = ae_get_movie_composition_data(mMovieData, name.c_str());
        if (compData) {
            result = new Composition();
            result->Create(mMovieData, compData);
        }
    }

    return result;
}

void Movie::CloseComposition(Composition* composition) {
    if (composition) {
        delete composition;
    }
}

bool Movie::OnProvideResource(const aeMovieResource* _resource, void** _rd, void* _ud) {
    AE_UNUSED(_ud);

    MyLog << "Resource provider callback." << MyEndl;

    switch (_resource->type) {
        case AE_MOVIE_RESOURCE_IMAGE: {
            const aeMovieResourceImage* ae_image = reinterpret_cast<const aeMovieResourceImage*>(_resource);

            MyLog << "Resource type: image." << MyEndl;
            MyLog << " path        : '" << ae_image->path << "'" << MyEndl;
            MyLog << " trim_width  : " << static_cast<int>(ae_image->trim_width) << MyEndl;
            MyLog << " trim_height : " << static_cast<int>(ae_image->trim_height) << MyEndl;
            MyLog << " has mesh    : " << (ae_image->mesh != nullptr ? "YES" : "NO") << MyEndl;

            std::string fullPath = mBaseFolder + ae_image->path;

            if (ae_image->atlas_image == AE_NULL) {
                *_rd = reinterpret_cast<ae_voidptr_t>(ResourcesManager::Instance().GetTextureRes(fullPath));
            } else {
                std::string texturePath = mBaseFolder + ae_image->atlas_image->path;

                ResourceImage* image = ResourcesManager::Instance().GetImageRes(ae_image->name);

                image->textureRes = ResourcesManager::Instance().GetTextureRes(texturePath);

                *_rd = reinterpret_cast<ae_voidptr_t>(image);
            }
        } break;

        case AE_MOVIE_RESOURCE_SEQUENCE: {
            MyLog << "Resource type: image sequence." << MyEndl;
        } break;

        case AE_MOVIE_RESOURCE_VIDEO: {
            const aeMovieResourceVideo * r = (const aeMovieResourceVideo *)_resource;

            MyLog << "Resource type: video." << MyEndl;
            MyLog << " path        : '" << r->path << "'" << MyEndl;
        } break;

        case AE_MOVIE_RESOURCE_SOUND: {
            const aeMovieResourceSound * r = (const aeMovieResourceSound *)_resource;

            MyLog << "Resource type: sound." << MyEndl;
            MyLog << " path        : '" << r->path << "'" << MyEndl;
        } break;

        case AE_MOVIE_RESOURCE_SLOT: {
            const aeMovieResourceSlot * r = (const aeMovieResourceSlot *)_resource;

            MyLog << "Resource type: slot." << MyEndl;
            MyLog << " width  = " << r->width << MyEndl;
            MyLog << " height = " << r->height << MyEndl;
        } break;

        default: {
            MyLog << "Resource type: other (" << _resource->type << ")" << MyEndl;
        } break;
    }

    return AE_TRUE;
}

void Movie::OnDeleteResource(const size_t _type, void* _data, void* _ud) {
    AE_UNUSED(_type);
    AE_UNUSED(_data);
    AE_UNUSED(_ud);
}
