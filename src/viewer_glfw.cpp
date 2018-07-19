#include <glad/glad.h>
#include <GLFW/glfw3.h>

extern "C" {
#include <movie/movie.h>
}

#include <nfd.h>

#include <iostream>
#include <cstdarg>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <chrono>

#include "movie_resmgr.h"
#include "movie.h"
#include "composition.h"

#define UI_SYSTEM_IMGUI     1
#define UI_SYSTEM_NUKLEAR   2
#define UI_SYSTEM           UI_SYSTEM_IMGUI

#if (UI_SYSTEM == UI_SYSTEM_IMGUI)
#include "imgui_impl_glfw_gl3_glad.h"
#elif (UI_SYSTEM == UI_SYSTEM_NUKLEAR)
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_GLFW_GL3_IMPLEMENTATION
#include <nuklear.h>

#define NK_GLFW_GL3_IMPLEMENTATION
#include "nuklear_glfw_gl3.h"

#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_ELEMENT_BUFFER 128 * 1024

//#NOTE_SK: since Nuklear doesn't have fps counter, lets do that ourself
struct FpsCounter {
    static const size_t kHistoryLen = 120;

    float  accumulator;
    size_t frameIdx;
    float  fps;
    float  history[kHistoryLen];
};

FpsCounter  gFpsCounter;
uint64_t    gLastFrameTime = 0;
#else
#error Please implement UI system!
#endif

struct UI {
    bool        showNormal;
    bool        showWireframe;
    bool        shouldExit;
    float       manualPlayPos;

    UI(): showNormal(true)
        , showWireframe(false)
        , shouldExit(false)
        , manualPlayPos(0.0f)
    {
    }
};

UI gUI;


static const size_t kWindowWidth = 1280;
static const size_t kWindowHeight = 720;

inline uint64_t GetCurrentTimeMilliseconds() {
    std::chrono::system_clock::time_point tp = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point::duration epoch = tp.time_since_epoch();

    std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(epoch);
    return static_cast<uint64_t>(ms.count());
}

std::string     gMovieFilePath;
std::string     gLicenseHash;
std::string     gCompositionName;
bool            gToLoopPlay = false;
Movie           gMovie;
Composition*    gComposition = nullptr;
size_t          gLastCompositionIdx = 0;
float           gBackgroundColor[3] = { 0.412f, 0.796f, 1.0f };


const char*     gSessionFileName = "session.txt";

void SaveSession() {
    FILE* f = my_fopen(gSessionFileName, "wt");
    if (f) {
        fprintf(f, "%s\n", gMovieFilePath.c_str());
        fprintf(f, "%s\n", gCompositionName.c_str());
        fprintf(f, "%s\n", gToLoopPlay ? "yes" : "no");
        fprintf(f, "%s\n", gLicenseHash.c_str());
        fprintf(f, "%f/%f/%f\n", gBackgroundColor[0], gBackgroundColor[1], gBackgroundColor[2]);
        fclose(f);
    }
}

void LoadSession() {
    FILE* f = my_fopen(gSessionFileName, "rt");
    if (f) {
        char line[1025] = { 0 };

        if (fgets(line, 1024, f)) {
            gMovieFilePath.assign(line, strlen(line) - 1);
        }
        if (fgets(line, 1024, f)) {
            gCompositionName.assign(line, strlen(line) - 1);
        }
        if (fgets(line, 1024, f)) {
            gToLoopPlay = (line[0] == 'y');
        }
        if (fgets(line, 1024, f)) {
            gLicenseHash.assign(line, strlen(line) - 1);
        }
        if (fgets(line, 1024, f)) {
            float temp[3] = { 0.0f };
            if (3 == my_sscanf(line, "%f/%f/%f", &temp[0], &temp[1], &temp[2])) {
                gBackgroundColor[0] = temp[0];
                gBackgroundColor[1] = temp[1];
                gBackgroundColor[2] = temp[2];
            }
        }

        fclose(f);
    }
}

void ShutdownMovie() {
    if (gComposition) {
        gMovie.CloseComposition(gComposition);
        gComposition = nullptr;
    }

    gMovie.Close();
    ResourcesManager::Instance().Shutdown();
}

void CalcScaleToFitComposition() {
    if (gComposition) {
        const float wndWidth = static_cast<float>(kWindowWidth);
        const float wndHeight = static_cast<float>(kWindowHeight);

        // Now we need to scale and position our content so that it's centered and fits the screen
        const float contentWidth = gComposition->GetWidth();
        const float contentHeight = gComposition->GetHeight();

        float scale = 1.0f;
        if (contentWidth > wndWidth || contentHeight > wndHeight) {
            // Content's size is bigger then the window, scaling needed
            const float dW = contentWidth - wndWidth;
            const float dH = contentHeight - wndHeight;

            if (dW > dH) {
                scale = wndWidth / contentWidth;
            } else {
                scale = wndHeight / contentHeight;
            }
        }

        gComposition->SetContentScale(scale);
    }
}

void CenterCompositionOnScreen() {
    if (gComposition) {
        const float wndWidth = static_cast<float>(kWindowWidth);
        const float wndHeight = static_cast<float>(kWindowHeight);

        const float contentWidth = gComposition->GetWidth();
        const float contentHeight = gComposition->GetHeight();
        const float contentScale = gComposition->GetContentScale();

        const float offX = (wndWidth - (contentWidth * contentScale)) * 0.5f;
        const float offY = (wndHeight - (contentHeight * contentScale)) * 0.5f;

        gComposition->SetContentOffset(offX, offY);
    }
}

void OnNewCompositionOpened() {
    if (gComposition) {
        const float wndWidth = static_cast<float>(kWindowWidth);
        const float wndHeight = static_cast<float>(kWindowHeight);

        gComposition->SetViewportSize(wndWidth, wndHeight);

        gCompositionName = gComposition->GetName();
        MyLog << "Composition \"" << gCompositionName << "\" loaded successfully" << MyEndl;
        MyLog << " Duration: " << gComposition->GetDuration() << " seconds" << MyEndl;

        gComposition->SetLoop(gToLoopPlay);
        gComposition->Play();

        // Now we need to scale and position our content so that it's centered and fits the screen
        CalcScaleToFitComposition();
        CenterCompositionOnScreen();
    }
}

bool ReloadMovie() {
    bool result = false;

    ShutdownMovie();

    ResourcesManager::Instance().Initialize();

    if (gMovie.LoadFromFile(gMovieFilePath, gLicenseHash)) {
        gComposition = gCompositionName.empty() ? gMovie.OpenDefaultComposition() : gMovie.OpenComposition(gCompositionName);
        if (gComposition) {
            OnNewCompositionOpened();

            SaveSession();
            gUI.manualPlayPos = 0.0f;

            gLastCompositionIdx = gMovie.FindMainCompositionIdx(gComposition);

            result = true;
        } else {
            MyLog << "Failed to open the default composition" << MyEndl;
        }
    } else {
        MyLog << "Failed to load the movie" << MyEndl;
    }

    return result;
}


void DoUI() {
#if (UI_SYSTEM == UI_SYSTEM_IMGUI)
    const ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;// | ImGuiWindowFlags_NoCollapse;

    const float rightPanelWidth = 200.0f;
    const float leftPanelWidth = 300.0f;
    const float panelHeight = 200.0f;

    float nextY = 0.0f;
    bool openNewMovie = false;

    ImGui::SetNextWindowPos(ImVec2(0.0f, nextY));
    ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, 0.0f));
    ImGui::Begin("Movie:", nullptr, kPanelFlags);
    {
        char moviePath[1024] = { 0 };
        char licenseHash[1024] = { 0 };
        char compositionName[1024] = { 0 };

        if (!gMovieFilePath.empty()) {
            memcpy(moviePath, gMovieFilePath.c_str(), gMovieFilePath.length());
        }
        if (!gLicenseHash.empty()) {
            memcpy(licenseHash, gLicenseHash.c_str(), gLicenseHash.length());
        }

        ImGui::Text("Movie file path:");
        ImGui::PushItemWidth(leftPanelWidth - 50.0f);
        {
            if (ImGui::InputText("##FilePath", moviePath, sizeof(moviePath) - 1)) {
                gMovieFilePath = moviePath;
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("...")) {
            nfdchar_t* outPath = nullptr;
            if (NFD_OKAY == NFD_OpenDialog("aem", nullptr, &outPath)) {
                gMovieFilePath = outPath;
                free(outPath);
                openNewMovie = true;
            }
        }

        ImGui::Text("Licence hash:");
        ImGui::PushItemWidth(leftPanelWidth - 50.0f);
        {
            if (ImGui::InputText("##LicenseHash", licenseHash, sizeof(licenseHash) - 1)) {
                gLicenseHash = licenseHash;
            }
        }
        ImGui::PopItemWidth();
    }
    nextY += ImGui::GetWindowHeight();
    ImGui::End();

    if (openNewMovie && !gMovieFilePath.empty() && !gLicenseHash.empty()) {
        gCompositionName.clear();
        ReloadMovie();
    }

    // If we have more then 1 main composition - let's allow user to choose one to play
    const size_t numMainCompositions = gMovie.GetMainCompositionsCount();
    if (numMainCompositions > 0) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, nextY));
        ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, panelHeight));
        ImGui::Begin("Main compositions:", nullptr, kPanelFlags);
        {
            int option = static_cast<int>(gLastCompositionIdx);

            for (size_t i = 0; i < numMainCompositions; ++i) {
                std::string guiID = std::to_string(i);

                std::string fullLabel = gMovie.GetMainCompositionNameByIdx(i) + "##" + guiID;
                ImGui::RadioButton(fullLabel.c_str(), &option, static_cast<int>(i));
            }

            if (static_cast<size_t>(option) != gLastCompositionIdx) {
                gLastCompositionIdx = static_cast<size_t>(option);
                gMovie.CloseComposition(gComposition);
                gComposition = gMovie.OpenMainCompositionByIdx(gLastCompositionIdx);
                OnNewCompositionOpened();
                gUI.manualPlayPos = 0.0f;
            }
        }
        ImGui::End();
    }


    nextY = 0.0f;
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kWindowWidth) - rightPanelWidth, nextY));
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, 0.0f));
    ImGui::Begin("Viewer:", nullptr, kPanelFlags);
    {
        ImGui::Text("%.1f FPS (%.3f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
        ImGui::Checkbox("Draw normal", &gUI.showNormal);
        ImGui::Checkbox("Draw wireframe", &gUI.showWireframe);
        {
            float contentScale = (gComposition == nullptr) ? 1.0f : gComposition->GetContentScale();
            ImGui::Text("Content scale:");
            ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.92f);
            ImGui::SliderFloat("##ContentScale", &contentScale, 0.0f, 10.0f);
            ImGui::PopItemWidth();
            if (gComposition) {
                gComposition->SetContentScale(contentScale);
                CenterCompositionOnScreen();
            }
        }
        ImGui::Text("Background color:");
        ImGui::ColorEdit3("##BkgColor", gBackgroundColor);
    }
    nextY += ImGui::GetWindowHeight();
    ImGui::End();

    if (gComposition) {
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kWindowWidth) - rightPanelWidth, nextY));
        ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, panelHeight));
        ImGui::Begin("Movie info:", nullptr, kPanelFlags);
        {
            const float duration = gComposition->GetDuration();
            const float playTime = gComposition->GetCurrentPlayTime();
            const float playPos = playTime / duration;

            ImGui::Text("Composition: %s", gComposition->GetName().c_str());
            ImGui::Text("Duration: %0.2fs", duration);
            ImGui::Text("Play time: %0.2fs", playTime);

            ImGui::ProgressBar(playPos);

            gUI.manualPlayPos = gComposition->GetCurrentPlayTime();
            ImGui::Text("Manual position:");
            ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.92f);
                ImGui::SliderFloat("##ManualPos", &gUI.manualPlayPos, 0.0f, gComposition->GetDuration());
            ImGui::PopItemWidth();
            gComposition->SetCurrentPlayTime(gUI.manualPlayPos);

            if (ImGui::Button("Play")) {
                if (gComposition->IsEndedPlay()) {
                    gUI.manualPlayPos = 0.0f;
                }
                gComposition->Play(gUI.manualPlayPos);
            }
            ImGui::SameLine();
            if (ImGui::Button("Pause")) {
                gComposition->Pause();
                gUI.manualPlayPos = gComposition->GetCurrentPlayTime();
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop")) {
                gComposition->Stop();
                gUI.manualPlayPos = 0.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Rewind")) {
                gComposition->SetCurrentPlayTime(0.0f);
                gUI.manualPlayPos = 0.0f;
            }

            bool loopComposition = gComposition->IsLooped();
            ImGui::Checkbox("Loop composition", &loopComposition);
            if (loopComposition != gComposition->IsLooped()) {
                gComposition->SetLoop(loopComposition);
                gToLoopPlay = loopComposition;
            }
        }
        nextY += ImGui::GetWindowHeight();
        ImGui::End();

        const size_t numSubCompositions = gComposition->GetNumSubCompositions();
        if (numSubCompositions) {
            ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kWindowWidth) - rightPanelWidth, nextY));
            ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, panelHeight));
            ImGui::Begin("Sub compositions:", nullptr, kPanelFlags);
            {
                for (size_t i = 0; i < numSubCompositions; ++i) {
                    std::string guiID = std::to_string(i);
                    std::string fullName = guiID + ") " + gComposition->GetSubCompositionName(i) + ":";
                    ImGui::Text(fullName.c_str());

                    std::string btnPlayNameAndId = std::string("Play##") + guiID;
                    std::string btnPauseNameAndId = std::string("Pause##") + guiID;
                    std::string btnStopNameAndId = std::string("Stop##") + guiID;
                    std::string btnRewindNameAndId = std::string("Rewind##") + guiID;

                    if (ImGui::Button(btnPlayNameAndId.c_str())) {
                        gComposition->PlaySubComposition(i);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(btnPauseNameAndId.c_str())) {
                        gComposition->PauseSubComposition(i);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(btnStopNameAndId.c_str())) {
                        gComposition->StopSubComposition(i);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(btnRewindNameAndId.c_str())) {
                        gComposition->SetTimeSubComposition(i, 0.0f);
                    }

                    std::string chkLoopNameAndId = std::string("Loop play##") + guiID;

                    bool loopPlay = gComposition->IsLoopedSubComposition(i);
                    ImGui::Checkbox(chkLoopNameAndId.c_str(), &loopPlay);
                    if (loopPlay != gComposition->IsLoopedSubComposition(i)) {
                        gComposition->SetLoopSubComposition(i, loopPlay);
                    }

                    ImGui::NewLine();
                }
            }
            ImGui::End();
        }
    }
#elif (UI_SYSTEM == UI_SYSTEM_NUKLEAR)
    // Calculate framerate
    if (!gLastFrameTime) {
        gLastFrameTime = GetCurrentTimeMilliseconds();
    } else {
        const uint64_t currentTime = GetCurrentTimeMilliseconds();
        const float deltaTime = static_cast<float>(currentTime - gLastFrameTime) * 0.001f;
        gLastFrameTime = currentTime;

        gFpsCounter.accumulator += deltaTime - gFpsCounter.history[gFpsCounter.frameIdx];
        gFpsCounter.history[gFpsCounter.frameIdx] = deltaTime;
        gFpsCounter.frameIdx = (gFpsCounter.frameIdx + 1) % FpsCounter::kHistoryLen;
        gFpsCounter.fps = (gFpsCounter.accumulator > 0.0f) ? (1.0f / (gFpsCounter.accumulator / static_cast<float>(FpsCounter::kHistoryLen))) : FLT_MAX;
    }

    const nk_flags kPanelFlags = NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MINIMIZABLE;
    const float kElementHeight = 22.0f;
    const float kLabelHeight = 16.0f;

    const float leftPanelWidth = 300.0f;
    const float rightPanelWidth = 260.0f;

    float nextY = 0.0f;
    bool openNewMovie = false;


    nk_context* ctx = nk_glfw3_get_ctx();

    struct nk_rect wndRect = nk_rect(0.0f, nextY, leftPanelWidth, 130.0f);
    if (nk_begin(ctx, "Movie:", wndRect, kPanelFlags)) {
        char moviePath[1024] = { 0 };
        char licenseHash[1024] = { 0 };
        char compositionName[1024] = { 0 };
        int len = 0;

        if (!gMovieFilePath.empty()) {
            memcpy(moviePath, gMovieFilePath.c_str(), gMovieFilePath.length());
    }
        if (!gLicenseHash.empty()) {
            memcpy(licenseHash, gLicenseHash.c_str(), gLicenseHash.length());
        }

        nk_layout_row_static(ctx, kLabelHeight, static_cast<int>(leftPanelWidth - 50.0f), 1);
        nk_label(ctx, "Movie file path:", NK_TEXT_LEFT);

        nk_layout_row_begin(ctx, NK_DYNAMIC, kElementHeight, 2);
        nk_layout_row_push(ctx, 0.85f);
        {
            len = nk_strlen(moviePath);
            nk_edit_string(ctx, NK_EDIT_SIMPLE, moviePath, &len, sizeof(moviePath) - 1, nk_filter_default);
            gMovieFilePath = moviePath;
        }

        nk_layout_row_push(ctx, 0.15f);
        if (nk_button_label(ctx, "...")) {
            nfdchar_t* outPath = nullptr;
            if (NFD_OKAY == NFD_OpenDialog("aem", nullptr, &outPath)) {
                gMovieFilePath = outPath;
                free(outPath);
                openNewMovie = true;
            }
        }

        nk_layout_row_dynamic(ctx, kLabelHeight, 1);
        nk_label(ctx, "Licence hash:", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, kElementHeight, 1);
        {
            len = nk_strlen(licenseHash);
            nk_edit_string(ctx, NK_EDIT_SIMPLE, licenseHash, &len, sizeof(licenseHash) - 1, nk_filter_default);
            gLicenseHash = licenseHash;
        }

        nextY += nk_window_get_height(ctx);
    } else {
        nextY += nk_window_get_content_region_min(ctx).y;
    }
    nk_end(ctx);

    if (openNewMovie && !gMovieFilePath.empty() && !gLicenseHash.empty()) {
        gCompositionName.clear();
        ReloadMovie();
    }

    // If we have more then 1 main composition - let's allow user to choose one to play
    const size_t numMainCompositions = gMovie.GetMainCompositionsCount();
    if (numMainCompositions > 0) {
        wndRect = nk_rect(0.0f, nextY, leftPanelWidth, 200.0f);
        if (nk_begin(ctx, "Main compositions:", wndRect, kPanelFlags)) {
            size_t savedOption = gLastCompositionIdx;

            for (size_t i = 0; i < numMainCompositions; ++i) {
                std::string label = gMovie.GetMainCompositionNameByIdx(i);

                nk_layout_row_dynamic(ctx, kElementHeight, 1);
                if (nk_option_label(ctx, label.c_str(), i == gLastCompositionIdx)) {
                    savedOption = i;
                }
            }

            if (savedOption != gLastCompositionIdx) {
                gLastCompositionIdx = savedOption;
                gMovie.CloseComposition(gComposition);
                gComposition = gMovie.OpenMainCompositionByIdx(gLastCompositionIdx);
                OnNewCompositionOpened();
                gUI.manualPlayPos = 0.0f;
            }
        } nk_end(ctx);
    }

    nextY = 0.0f;
    wndRect = nk_rect(static_cast<float>(kWindowWidth) - rightPanelWidth, nextY, rightPanelWidth, 250.0f);
    if (nk_begin(ctx, "Viewer:", wndRect, kPanelFlags)) {
        nk_layout_row_dynamic(ctx, kLabelHeight, 1);
        nk_labelf(ctx, NK_TEXT_LEFT, "%.1f FPS (%.3f ms)", gFpsCounter.fps, 1000.0f / gFpsCounter.fps);

        nk_layout_row_dynamic(ctx, kElementHeight, 1);
        {
            int check = gUI.showNormal ? nk_true : nk_false;
            nk_checkbox_label(ctx, "Draw normal", &check);
            gUI.showNormal = (check == nk_true);
        }
        {
            int check = gUI.showWireframe ? nk_true : nk_false;
            nk_checkbox_label(ctx, "Draw wireframe", &check);
            gUI.showWireframe = (check == nk_true);
        }

        {
            float contentScale = (gComposition == nullptr) ? 1.0f : gComposition->GetContentScale();
            nk_layout_row_dynamic(ctx, kLabelHeight, 1);
            nk_labelf(ctx, NK_TEXT_LEFT, "Content scale: %0.4f", contentScale);
            nk_slider_float(ctx, 0.1f, &contentScale, 10.0f, 0.005f);
            if (gComposition) {
                gComposition->SetContentScale(contentScale);
                CenterCompositionOnScreen();
            }
        }

        nk_layout_row_dynamic(ctx, kLabelHeight, 1);
        nk_label(ctx, "Background color:", NK_TEXT_LEFT);
        {
            nk_colorf color;
            color.r = gBackgroundColor[0];
            color.g = gBackgroundColor[1];
            color.b = gBackgroundColor[2];
            color.a = 1.0f;

            nk_layout_row_static(ctx, kElementHeight * 3.5f, static_cast<int>(rightPanelWidth - 50.0f), 1);
            if (nk_color_pick(ctx, &color, NK_RGB)) {
                gBackgroundColor[0] = color.r;
                gBackgroundColor[1] = color.g;
                gBackgroundColor[2] = color.b;
            }
        }
        nextY += nk_window_get_height(ctx);
    } else {
        nextY += nk_window_get_content_region_min(ctx).y;
    }
    nk_end(ctx);

    if (gComposition) {
        wndRect = nk_rect(static_cast<float>(kWindowWidth) - rightPanelWidth, nextY, rightPanelWidth, 210.0f);
        if (nk_begin(ctx, "Movie info:", wndRect, kPanelFlags)) {
            const float duration = gComposition->GetDuration();
            const float playTime = gComposition->GetCurrentPlayTime();
            const float playPos = playTime / duration;

            nk_layout_row_dynamic(ctx, kLabelHeight, 1);
            nk_labelf(ctx, NK_TEXT_LEFT, "Composition: %s", gComposition->GetName().c_str());
            nk_labelf(ctx, NK_TEXT_LEFT, "Duration: %0.2fs", duration);
            nk_labelf(ctx, NK_TEXT_LEFT, "Play time: %0.2fs", playTime);

            nk_layout_row_dynamic(ctx, kLabelHeight, 1);
            nk_prog(ctx, static_cast<nk_size>(playPos * 1000.0f), 1000, nk_false);

            gUI.manualPlayPos = gComposition->GetCurrentPlayTime();
            nk_layout_row_dynamic(ctx, kLabelHeight, 1);
            nk_label(ctx, "Manual position:", NK_TEXT_LEFT);

            nk_layout_row_dynamic(ctx, kLabelHeight, 1);
            nk_slider_float(ctx, 0.0f, &gUI.manualPlayPos, gComposition->GetDuration(), gComposition->GetDuration() * 0.005f);
            gComposition->SetCurrentPlayTime(gUI.manualPlayPos);

            nk_layout_row_begin(ctx, NK_DYNAMIC, kLabelHeight, 4);
            nk_layout_row_push(ctx, 0.25f);
            if (nk_button_label(ctx, "Play")) {
                if (gComposition->IsEndedPlay()) {
                    gUI.manualPlayPos = 0.0f;
                }
                gComposition->Play(gUI.manualPlayPos);
            }
            nk_layout_row_push(ctx, 0.25f);
            if (nk_button_label(ctx, "Pause")) {
                gComposition->Pause();
                gUI.manualPlayPos = gComposition->GetCurrentPlayTime();
            }
            nk_layout_row_push(ctx, 0.25f);
            if (nk_button_label(ctx, "Stop")) {
                gComposition->Stop();
                gUI.manualPlayPos = 0.0f;
            }
            nk_layout_row_push(ctx, 0.25f);
            if (nk_button_label(ctx, "Rewind")) {
                gComposition->SetCurrentPlayTime(0.0f);
                gUI.manualPlayPos = 0.0f;
            }

            nk_layout_row_dynamic(ctx, kElementHeight, 1);
            {
                int check = gComposition->IsLooped() ? nk_true : nk_false;
                nk_checkbox_label(ctx, "Loop composition", &check);
                if (gComposition->IsLooped() != (check == nk_true)) {
                    gToLoopPlay = (check == nk_true);
                    gComposition->SetLoop(gToLoopPlay);
                }
            }
            nextY += nk_window_get_height(ctx);
        } else {
            nextY += nk_window_get_content_region_min(ctx).y;
        }
        nk_end(ctx);

        const size_t numSubCompositions = gComposition->GetNumSubCompositions();
        if (numSubCompositions) {
            wndRect = nk_rect(static_cast<float>(kWindowWidth) - rightPanelWidth, nextY, rightPanelWidth, 210.0f);
            if (nk_begin(ctx, "Sub compositions:", wndRect, kPanelFlags)) {
                for (size_t i = 0; i < numSubCompositions; ++i) {
                    std::string fullName = std::to_string(i) + ") " + gComposition->GetSubCompositionName(i) + ":";
                    nk_layout_row_dynamic(ctx, kLabelHeight, 1);
                    nk_label(ctx, fullName.c_str(), NK_TEXT_LEFT);

                    nk_layout_row_begin(ctx, NK_DYNAMIC, kLabelHeight, 4);
                    nk_layout_row_push(ctx, 0.25f);
                    if (nk_button_label(ctx, "Play")) {
                        gComposition->PlaySubComposition(i);
                    }
                    nk_layout_row_push(ctx, 0.25f);
                    if (nk_button_label(ctx, "Pause")) {
                        gComposition->PauseSubComposition(i);
                    }
                    nk_layout_row_push(ctx, 0.25f);
                    if (nk_button_label(ctx, "Stop")) {
                        gComposition->StopSubComposition(i);
                    }
                    nk_layout_row_push(ctx, 0.25f);
                    if (nk_button_label(ctx, "Rewind")) {
                        gComposition->SetTimeSubComposition(i, 0.0f);
                    }

                    nk_layout_row_dynamic(ctx, kElementHeight, 1);
                    {
                        int check = gComposition->IsLoopedSubComposition(i) ? nk_true : nk_false;
                        nk_checkbox_label(ctx, "Loop play", &check);
                        if (gComposition->IsLoopedSubComposition(i) != (check == nk_true)) {
                            gComposition->SetLoopSubComposition(i, check == nk_true);
                        }
                    }
                }
            }
            nk_end(ctx);
        }
    }
#else
#error Please implement UI system!
#endif
}

#if (UI_SYSTEM == UI_SYSTEM_NUKLEAR)
void SetupNuklearBlueTheme() {
    nk_context* ctx = nk_glfw3_get_ctx();

    nk_color table[NK_COLOR_COUNT];
    table[NK_COLOR_TEXT] = nk_rgba(20, 20, 20, 255);
    table[NK_COLOR_WINDOW] = nk_rgba(202, 212, 214, 215);
    table[NK_COLOR_HEADER] = nk_rgba(137, 182, 224, 220);
    table[NK_COLOR_BORDER] = nk_rgba(140, 159, 173, 255);
    table[NK_COLOR_BUTTON] = nk_rgba(137, 182, 224, 255);
    table[NK_COLOR_BUTTON_HOVER] = nk_rgba(142, 187, 229, 255);
    table[NK_COLOR_BUTTON_ACTIVE] = nk_rgba(147, 192, 234, 255);
    table[NK_COLOR_TOGGLE] = nk_rgba(177, 210, 210, 255);
    table[NK_COLOR_TOGGLE_HOVER] = nk_rgba(182, 215, 215, 255);
    table[NK_COLOR_TOGGLE_CURSOR] = nk_rgba(137, 182, 224, 255);
    table[NK_COLOR_SELECT] = nk_rgba(177, 210, 210, 255);
    table[NK_COLOR_SELECT_ACTIVE] = nk_rgba(137, 182, 224, 255);
    table[NK_COLOR_SLIDER] = nk_rgba(177, 210, 210, 255);
    table[NK_COLOR_SLIDER_CURSOR] = nk_rgba(137, 182, 224, 245);
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgba(142, 188, 229, 255);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgba(147, 193, 234, 255);
    table[NK_COLOR_PROPERTY] = nk_rgba(210, 210, 210, 255);
    table[NK_COLOR_EDIT] = nk_rgba(210, 210, 210, 225);
    table[NK_COLOR_EDIT_CURSOR] = nk_rgba(20, 20, 20, 255);
    table[NK_COLOR_COMBO] = nk_rgba(210, 210, 210, 255);
    table[NK_COLOR_CHART] = nk_rgba(210, 210, 210, 255);
    table[NK_COLOR_CHART_COLOR] = nk_rgba(137, 182, 224, 255);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgba(255, 0, 0, 255);
    table[NK_COLOR_SCROLLBAR] = nk_rgba(190, 200, 200, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgba(64, 84, 95, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba(70, 90, 100, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(75, 95, 105, 255);
    table[NK_COLOR_TAB_HEADER] = nk_rgba(156, 193, 220, 255);
    nk_style_from_table(ctx, table);

    //#NOTE_SK: !undocumented! - changing window title padding (default is nk_vec2(4, 4))
    ctx->style.window.header.label_padding = nk_vec2(1.5f, 1.5f);
    ctx->style.window.header.padding = nk_vec2(1.5f, 1.5f);

}
#endif

int main(int argc, char** argv) {
    std::string compositionName;
    std::string isLooped;

    if(argc == 5) {
        gMovieFilePath   = argv[1];
        gCompositionName = argv[2];
        isLooped         = argv[3];
        gLicenseHash     = argv[4];

        std::transform(isLooped.begin(), isLooped.end(), isLooped.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });

        gToLoopPlay = (isLooped == "true" || isLooped == "1" || isLooped == "yes");
    } else {
        LoadSession();
    }

    if (!glfwInit()) {
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    GLFWwindow* window = glfwCreateWindow(static_cast<int>(kWindowWidth),
                                          static_cast<int>(kWindowHeight),
                                          "libMovie - GLFW viewer",
                                          nullptr,
                                          nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));
    glfwSwapInterval(1); // enable v-sync

#if (UI_SYSTEM == UI_SYSTEM_IMGUI)
    glfwSetScrollCallback(window, ImGui_ImplGlfw_ScrollCallback);
    glfwSetCharCallback(window, ImGui_ImplGlfw_CharCallback);
    glfwSetKeyCallback(window, ImGui_ImplGlfw_KeyCallback);
    glfwSetMouseButtonCallback(window, ImGui_ImplGlfw_MouseButtonCallback);

    // Setup Dear ImGui binding
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // disable "imgui.ini"
    ImGui_ImplGlfwGL3_Init(window, false);
    ImGui::StyleColorsClassic();
#elif (UI_SYSTEM == UI_SYSTEM_NUKLEAR)
    /*nk_context* ctx = */nk_glfw3_init(window, NK_GLFW3_INSTALL_CALLBACKS);
    {
        struct nk_font_atlas *atlas;
        nk_glfw3_font_stash_begin(&atlas);
        nk_glfw3_font_stash_end();
    }
    SetupNuklearBlueTheme();

    gFpsCounter.accumulator = 0.0f;
    gFpsCounter.frameIdx = 0;
    gFpsCounter.fps = 0.0f;
#endif

    ResourcesManager::Instance().Initialize();

    if (!gMovieFilePath.empty() && !gLicenseHash.empty()) {
        ReloadMovie();
    }

    glViewport(0, 0, static_cast<GLint>(kWindowWidth), static_cast<GLint>(kWindowHeight));

    uint64_t timeLast = GetCurrentTimeMilliseconds();
    while (!glfwWindowShouldClose(window) && !gUI.shouldExit) {
        glfwPollEvents();

        glClearColor(gBackgroundColor[0], gBackgroundColor[1], gBackgroundColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const uint64_t timeNow = GetCurrentTimeMilliseconds();
        const float dt = static_cast<float>(timeNow - timeLast) * 0.001f;
        timeLast = timeNow;

#if (UI_SYSTEM == UI_SYSTEM_IMGUI)
        ImGui_ImplGlfwGL3_NewFrame();
#elif (UI_SYSTEM == UI_SYSTEM_NUKLEAR)
        nk_glfw3_new_frame();
#endif

        if (gComposition) {
            if (gComposition->IsPlaying()) {
                gComposition->Update(dt);
            }

            if (gUI.showNormal || gUI.showWireframe) {
                Composition::DrawMode drawMode;
                if (gUI.showNormal && gUI.showWireframe) {
                    drawMode = Composition::DrawMode::SolidWithWireOverlay;
                } else if (gUI.showWireframe) {
                    drawMode = Composition::DrawMode::Wireframe;
                } else {
                    drawMode = Composition::DrawMode::Solid;
                }

                gComposition->Draw(drawMode);
            }
        }

        DoUI();

#if (UI_SYSTEM == UI_SYSTEM_IMGUI)
        ImGui::Render();
        ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
#elif (UI_SYSTEM == UI_SYSTEM_NUKLEAR)
        nk_glfw3_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
#endif

        glfwSwapBuffers(window);
    }

    // save session on exit
    if (!gMovieFilePath.empty() && !gLicenseHash.empty()) {
        SaveSession();
    }

    ShutdownMovie();

#if (UI_SYSTEM == UI_SYSTEM_IMGUI)
    ImGui_ImplGlfwGL3_Shutdown();
    ImGui::DestroyContext();
#elif (UI_SYSTEM == UI_SYSTEM_NUKLEAR)
    nk_glfw3_shutdown();
#endif

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
