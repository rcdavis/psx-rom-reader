#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include "psxrom/psxrom.h"

#include <cstdio>

namespace
{

bool set_gl_attribute(SDL_GLAttr attribute, int value)
{
    if (SDL_GL_SetAttribute(attribute, value))
        return true;

    std::fprintf(stderr, "SDL_GL_SetAttribute failed: %s\n", SDL_GetError());
    return false;
}

bool run_viewer(SDL_Window* window)
{
    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window)))
            {
                running = false;
            }
        }

        if (!running)
            break;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("PSX ROM Reader");
        ImGui::TextUnformatted("SDL3 and Dear ImGui are ready.");
        ImGui::Text("Reader core: %s", psxrom_strerror(PSXROM_OK));
        ImGui::TextUnformatted("Disc browsing UI is not implemented yet.");
        ImGui::End();

        ImGui::Render();

        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &width, &height))
        {
            std::fprintf(stderr, "SDL_GetWindowSizeInPixels failed: %s\n", SDL_GetError());
            return false;
        }
        glViewport(0, 0, width, height);
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!SDL_GL_SwapWindow(window))
        {
            std::fprintf(stderr, "SDL_GL_SwapWindow failed: %s\n", SDL_GetError());
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    if (!set_gl_attribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
        !set_gl_attribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) ||
        !set_gl_attribute(SDL_GL_CONTEXT_MINOR_VERSION, 2) ||
        !set_gl_attribute(SDL_GL_DOUBLEBUFFER, 1))
    {
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "PSX ROM Reader", 1280, 800,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr)
    {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!SDL_GL_MakeCurrent(window, gl_context))
    {
        std::fprintf(stderr, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (!SDL_GL_SetSwapInterval(1))
        std::fprintf(stderr, "Warning: could not enable vertical sync: %s\n", SDL_GetError());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForOpenGL(window, gl_context))
    {
        std::fprintf(stderr, "ImGui SDL3 backend initialization failed\n");
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 150"))
    {
        std::fprintf(stderr, "ImGui OpenGL3 backend initialization failed\n");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const bool success = run_viewer(window);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return success ? 0 : 1;
}
