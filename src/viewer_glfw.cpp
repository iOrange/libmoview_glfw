#include <glad/glad.h>
#include <GLFW/glfw3.h>

extern "C" {
#include <movie/movie.h>
}

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
    bool showNormal;
    bool showWireframe;
};


static const size_t kWindowWidth = 1280;
static const size_t kWindowHeight = 720;

inline float GetCurrentTimeSeconds() {
    clock_t c = clock();
    return static_cast<float>(c) / static_cast<float>(CLOCKS_PER_SEC);
}

void DoUI(UI& ui, Composition* composition) {
    const ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;// | ImGuiWindowFlags_NoCollapse;
    const float panelWidth = 200.0f;
    const float panelHeight = 200.0f;

    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kWindowWidth) - panelWidth, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));
    ImGui::Begin("Movie info", nullptr, kPanelFlags);
    {
        const float duration = composition->GetDuration();
        const float playTime = composition->GetCurrentPlayTime();
        const float playPos = playTime / duration;

        ImGui::Text("Composition: %s", composition->GetName().c_str());
        ImGui::Text("Duration: %0.2fs", duration);
        ImGui::Text("Play time: %0.2fs", playTime);

        ImGui::ProgressBar(playPos);

        if (ImGui::Button("Play")) {
            composition->Play();
        }
        ImGui::SameLine();
        if (ImGui::Button("Pause")) {
            composition->Pause();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            composition->Stop();
        }

        bool loopComposition = composition->IsLooped();
        ImGui::Checkbox("Loop composition", &loopComposition);
        if (loopComposition != composition->IsLooped()) {
            composition->SetLoop(loopComposition);
        }
    }
    const float nextY = ImGui::GetWindowHeight();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kWindowWidth) - panelWidth, nextY));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));
    ImGui::Begin("Sub compositions", nullptr, kPanelFlags);
    {
        const size_t numSubCompositions = composition->GetNumSubCompositions();
        for (size_t i = 0; i < numSubCompositions; ++i) {
            std::string guiID = std::to_string(i);
            std::string fullName = guiID + ") " + composition->GetSubCompositionName(i) + ":";
            ImGui::Text(fullName.c_str());

            std::string btnPlayNameAndId = std::string("Play##") + guiID;
            std::string btnPauseNameAndId = std::string("Pause##") + guiID;
            std::string btnStopNameAndId = std::string("Stop##") + guiID;
            std::string btnRewindNameAndId = std::string("Rewind##") + guiID;

            if (ImGui::Button(btnPlayNameAndId.c_str())) {
                composition->PlaySubComposition(i);
            }
            ImGui::SameLine();
            if (ImGui::Button(btnPauseNameAndId.c_str())) {
                composition->PauseSubComposition(i);
            }
            ImGui::SameLine();
            if (ImGui::Button(btnStopNameAndId.c_str())) {
                composition->StopSubComposition(i);
            }
            ImGui::SameLine();
            if (ImGui::Button(btnRewindNameAndId.c_str())) {
                composition->SetTimeSubComposition(i, 0.0f);
            }

            std::string chkLoopNameAndId = std::string("Loop play##") + guiID;

            bool loopPlay = composition->IsLoopedSubComposition(i);
            ImGui::Checkbox(chkLoopNameAndId.c_str(), &loopPlay);
            if (loopPlay != composition->IsLoopedSubComposition(i)) {
                composition->SetLoopSubComposition(i, loopPlay);
            }

            ImGui::NewLine();
        }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(150.0f, 0.0f));
    ImGui::Begin("Viewer:", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::Text("%.1f FPS (%.3f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Checkbox("Draw normal", &ui.showNormal);
    ImGui::Checkbox("Draw wireframe", &ui.showWireframe);
    ImGui::End();
}

int main(int argc, char** argv) {
    std::string pathToFile;
    std::string compositionName;
    std::string isLooped;
    std::string licenseHash;

    bool toLoop = false;

    if(argc != 5) {
        std::cout << "Usage:  viewer.exe path_to_file compositions_name is_looped hash" << std::endl;
        return -1;
    } else {
        pathToFile      = argv[1];
        compositionName = argv[2];
        isLooped        = argv[3];
        licenseHash     = argv[4];

        std::transform(isLooped.begin(), isLooped.end(), isLooped.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });

        toLoop = (isLooped == "true" || isLooped == "1" || isLooped == "yes");
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

    UI ui;
    ui.showNormal = true;
    ui.showWireframe = false;

    // Setup Dear ImGui binding
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfwGL3_Init(window, false);
    ImGui::StyleColorsClassic();

    Movie movie;
    if(movie.LoadFromFile(pathToFile, licenseHash)) {
        MyLog << MyEndl << MyEndl << MyEndl;
        MyLog << "Loaded movie ver. " << movie.GetVersion() << MyEndl;

        Composition* composition = movie.OpenComposition(compositionName);
        if (composition) {
            MyLog << "Composition \"" << composition->GetName() << "\" loaded successfully" << MyEndl;
            MyLog << " Duration: " << composition->GetDuration() << " seconds" << MyEndl;

            composition->SetLoop(toLoop);
            composition->Play();

            glViewport(0, 0, static_cast<GLint>(kWindowWidth), static_cast<GLint>(kWindowHeight));
            glClearColor(0.412f, 0.796f, 1.0f, 1.0f);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            ResourcesManager::Instance().Initialize();

            float timeLast = GetCurrentTimeSeconds();
            while (!glfwWindowShouldClose(window)) {
                glfwPollEvents();

                glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

                const float timeNow = GetCurrentTimeSeconds();
                const float dt = timeNow - timeLast;
                timeLast = timeNow;

                ImGui_ImplGlfwGL3_NewFrame();

                if (composition->IsPlaying()) {
                    composition->Update(dt);
                }

                if (ui.showNormal || ui.showWireframe) {
                    Composition::DrawMode drawMode;
                    if (ui.showNormal && ui.showWireframe) {
                        drawMode = Composition::DrawMode::SolidWithWireOverlay;
                    } else if (ui.showWireframe) {
                        drawMode = Composition::DrawMode::Wireframe;
                    } else {
                        drawMode = Composition::DrawMode::Solid;
                    }
                    composition->Draw(drawMode);
                }

                DoUI(ui, composition);

                ImGui::Render();
                ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());

                glfwSwapBuffers(window);
            }

            movie.CloseComposition(composition);
            ResourcesManager::Instance().Shutdown();
        } else {
            MyLog << "Failed to open the composition" << MyEndl;
        }

        movie.Close();
    } else {
        MyLog << "Failed to load the movie" << MyEndl;
    }

    ImGui_ImplGlfwGL3_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}