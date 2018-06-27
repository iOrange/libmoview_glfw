#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdio>
#include <iostream>

inline FILE* my_fopen(const char* fileName, const char* mode) {
#if _MSC_VER >= 1400
    FILE* f = nullptr;
    if(fopen_s(&f, fileName, mode)) {
        return nullptr;
    } else {
        return f;
    }
#else
    return fopen(fileName, mode);
#endif
}

#if _MSC_VER >= 1400
#define my_sscanf sscanf_s
#else
#define my_sscanf sscanf
#endif

#define MyLog   std::cout
#define MyEndl  std::endl

