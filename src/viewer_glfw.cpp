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

#include "movie_resmgr.h"
#include "movie.h"
#include "composition.h"

#include "imgui_impl_glfw_gl3_glad.h"

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

inline float GetCurrentTimeSeconds() {
    clock_t c = clock();
    return static_cast<float>(c) / static_cast<float>(CLOCKS_PER_SEC);
}

std::string     gMovieFilePath;
std::string     gLicenseHash;
std::string     gCompositionName;
bool            gToLoopPlay = false;
Movie           gMovie;
Composition*    gComposition = nullptr;
size_t          gLastCompositionIdx = 0;


const char*     gSessionFileName = "session.txt";

void SaveSession() {
    FILE* f = my_fopen(gSessionFileName, "wt");
    if (f) {
        fprintf(f, "%s\n", gMovieFilePath.c_str());
        fprintf(f, "%s\n", gCompositionName.c_str());
        fprintf(f, "%s\n", gToLoopPlay ? "yes" : "no");
        fprintf(f, "%s\n", gLicenseHash.c_str());
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

bool ReloadMovie() {
    bool result = false;

    ShutdownMovie();

    ResourcesManager::Instance().Initialize();

    if (gMovie.LoadFromFile(gMovieFilePath, gLicenseHash)) {
        gComposition = gCompositionName.empty() ? gMovie.OpenDefaultComposition() : gMovie.OpenComposition(gCompositionName);
        if (gComposition) {
            gCompositionName = gComposition->GetName();
            MyLog << "Composition \"" << gCompositionName << "\" loaded successfully" << MyEndl;
            MyLog << " Duration: " << gComposition->GetDuration() << " seconds" << MyEndl;
            gComposition->SetLoop(gToLoopPlay);
            gComposition->Play();

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
        if (!gCompositionName.empty()) {
            memcpy(compositionName, gCompositionName.c_str(), gCompositionName.length());
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

        ImGui::Text("Composition name:");
        ImGui::PushItemWidth(leftPanelWidth - 50.0f);
        {
            if (ImGui::InputText("##CompositionName", compositionName, sizeof(compositionName) - 1)) {
                gCompositionName = compositionName;
            }
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Leave it empty to play default composition");
        }

        ImGui::NewLine();
        if (ImGui::Button("Play the movie !")) {
            openNewMovie = true;
        }
    }
    nextY += ImGui::GetWindowHeight();
    ImGui::End();

    if (openNewMovie && !gMovieFilePath.empty() && !gLicenseHash.empty()) {
        ReloadMovie();
    }

    // If we have more then 1 main composition - let's allow user to choose one to play
    const size_t numMainCompositions = gMovie.GetMainCompositionsCount();
    if (numMainCompositions > 1) {
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
                gComposition->SetLoop(gToLoopPlay);
                gComposition->Play();
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
            ImGui::SliderFloat("##ManualPos", &gUI.manualPlayPos, 0.0f, gComposition->GetDuration());
            gComposition->SetCurrentPlayTime(gUI.manualPlayPos);

            if (ImGui::Button("Play")) {
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

            bool loopComposition = gComposition->IsLooped();
            ImGui::Checkbox("Loop composition", &loopComposition);
            if (loopComposition != gComposition->IsLooped()) {
                gComposition->SetLoop(loopComposition);
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
}

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

    ResourcesManager::Instance().Initialize();

    if (!gMovieFilePath.empty() && !gLicenseHash.empty()) {
        ReloadMovie();
    }

    glViewport(0, 0, static_cast<GLint>(kWindowWidth), static_cast<GLint>(kWindowHeight));
    glClearColor(0.412f, 0.796f, 1.0f, 1.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float timeLast = GetCurrentTimeSeconds();
    while (!glfwWindowShouldClose(window) && !gUI.shouldExit) {
        glfwPollEvents();

        glClear(GL_COLOR_BUFFER_BIT);

        const float timeNow = GetCurrentTimeSeconds();
        const float dt = timeNow - timeLast;
        timeLast = timeNow;

        ImGui_ImplGlfwGL3_NewFrame();

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

        ImGui::Render();
        ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ShutdownMovie();

    ImGui_ImplGlfwGL3_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
