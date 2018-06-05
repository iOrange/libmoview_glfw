#pragma once
#include "utils.h"

class Composition;

struct aeMovieInstance;
struct aeMovieData;
struct aeMovieResource;
struct aeMovieCompositionData;

class Movie {
public:
    Movie();
    ~Movie();

    bool            LoadFromFile(const std::string& fileName, const std::string& licenseHash);
    bool            LoadFromMemory(const void* data, const size_t dataLength, const std::string& baseFolder, const std::string& licenseHash);
    void            Close();

    float           GetVersion() const;

    Composition*    OpenComposition(const std::string& name);
    void            CloseComposition(Composition* composition);

    Composition*    OpenDefaultComposition();

private:
    void            AddCompositionData(const aeMovieCompositionData* compositionData);

    bool            OnProvideResource(const aeMovieResource* _resource, void** _rd, void* _ud);
    void            OnDeleteResource(const size_t _type, void* _data, void* _ud);

private:
    const aeMovieInstance*                      mMovieInstance;
    aeMovieData*                                mMovieData;
    float                                       mVersion;
    std::string                                 mBaseFolder;

    std::vector<const aeMovieCompositionData*>  mCompositions;
};
