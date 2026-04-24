workspace "VkAmareloGPU"
    architecture "x64"
    configurations { "Debug", "Release", "Dist" }
    startproject "engine"

    flags { "MultiProcessorCompile" }

    filter "configurations:Debug"
        defines { "DEBUG" }
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        defines { "NDEBUG" }
        runtime "Release"
        optimize "on"

    filter "configurations:Dist"
        defines { "NDEBUG" }
        runtime "Release"
        optimize "Full"

    filter {}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

VULKAN_SDK = os.getenv("VULKAN_SDK")

-- ============================================================
-- vkbootstrap
-- ============================================================
project "vkbootstrap"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir    ("bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "third_party/vkbootstrap/VkBootstrap.h",
        "third_party/vkbootstrap/VkBootstrap.cpp",
    }

    includedirs { "third_party/vkbootstrap" }

    sysincludedirs { VULKAN_SDK .. "/Include" }
    libdirs        { VULKAN_SDK .. "/Lib" }
    links          { "vulkan-1" }

    filter "system:windows"
        systemversion "latest"
        staticruntime "off"


-- ============================================================
-- fmt
-- ============================================================
project "fmt"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir    ("bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "third_party/fmt/src/format.cc",
        "third_party/fmt/src/os.cc",
        "third_party/fmt/include/**.h",
    }

    includedirs { "third_party/fmt/include" }

    filter "system:windows"
        systemversion "latest"
        staticruntime "off"


-- ============================================================
-- SDL2  (Windows-only whitelist — avoids pulling in platform
--        headers for PS2, Linux, macOS, etc.)
-- ============================================================
project "SDL2"
    kind "StaticLib"
    language "C++"   -- project default; .c files still compile as C via extension
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir    ("bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        -- root
        "third_party/SDL/src/*.c",
        -- subsystems: core files (no platform subdirs)
        "third_party/SDL/src/atomic/*.c",
        "third_party/SDL/src/cpuinfo/*.c",
        "third_party/SDL/src/events/*.c",
        "third_party/SDL/src/file/*.c",
        "third_party/SDL/src/haptic/*.c",
        "third_party/SDL/src/libm/*.c",
        "third_party/SDL/src/locale/*.c",
        "third_party/SDL/src/main/*.c",
        "third_party/SDL/src/misc/*.c",
        "third_party/SDL/src/power/*.c",
        "third_party/SDL/src/render/*.c",
        "third_party/SDL/src/sensor/*.c",
        "third_party/SDL/src/stdlib/*.c",
        "third_party/SDL/src/thread/*.c",
        "third_party/SDL/src/timer/*.c",
        "third_party/SDL/src/video/*.c",
        -- audio: core + Windows backends
        "third_party/SDL/src/audio/*.c",
        "third_party/SDL/src/audio/directsound/*.c",
        "third_party/SDL/src/audio/disk/*.c",
        "third_party/SDL/src/audio/dummy/*.c",
        "third_party/SDL/src/audio/wasapi/*.c",   -- .c only; wasapi_winrt.cpp excluded
        "third_party/SDL/src/audio/winmm/*.c",
        -- core: Win32 only
        "third_party/SDL/src/core/windows/*.c",
        -- filesystem
        "third_party/SDL/src/filesystem/windows/*.c",
        -- haptic
        "third_party/SDL/src/haptic/windows/*.c",
        -- hidapi: SDL wrapper + Windows backend
        "third_party/SDL/src/hidapi/SDL_hidapi.c",
        "third_party/SDL/src/hidapi/windows/*.c",
        -- joystick: core + hidapi devices + virtual + Windows backends
        "third_party/SDL/src/joystick/*.c",
        "third_party/SDL/src/joystick/hidapi/*.c",
        "third_party/SDL/src/joystick/virtual/*.c",
        "third_party/SDL/src/joystick/windows/*.c",
        -- loadso
        "third_party/SDL/src/loadso/windows/*.c",
        -- locale
        "third_party/SDL/src/locale/windows/*.c",
        -- main
        "third_party/SDL/src/main/windows/*.c",
        -- misc
        "third_party/SDL/src/misc/windows/*.c",
        -- power
        "third_party/SDL/src/power/windows/*.c",
        -- render: D3D + software only (OpenGL/ES disabled via defines below)
        "third_party/SDL/src/render/direct3d/*.c",
        "third_party/SDL/src/render/direct3d11/*.c",
        "third_party/SDL/src/render/direct3d12/*.c",  -- .c only; xbox .cpp excluded
        "third_party/SDL/src/render/software/*.c",
        -- sensor: Windows + dummy (SDL_SENSOR_WINDOWS + SDL_SENSOR_DUMMY)
        "third_party/SDL/src/sensor/dummy/*.c",
        "third_party/SDL/src/sensor/windows/*.c",
        -- thread: Windows + generic condition variable (SDL_THREAD_GENERIC_COND_SUFFIX)
        "third_party/SDL/src/thread/windows/*.c",
        "third_party/SDL/src/thread/generic/SDL_syscond.c",
        -- timer
        "third_party/SDL/src/timer/windows/*.c",
        -- video
        "third_party/SDL/src/video/dummy/*.c",
        "third_party/SDL/src/video/windows/*.c",
        "third_party/SDL/src/video/yuv2rgb/*.c",
    }

    includedirs {
        "third_party/SDL/include",
        "third_party/SDL/src",
        "third_party/SDL/src/hidapi/hidapi",
    }

    defines {
        "SDL_STATIC",
        "SDL_STATIC_LIB",
        -- disable unused renderers so their source files aren't needed
        "SDL_VIDEO_RENDER_OGL=0",
        "SDL_VIDEO_RENDER_OGL_ES2=0",
        "SDL_VIDEO_OPENGL=0",
        "SDL_VIDEO_OPENGL_WGL=0",
        "SDL_VIDEO_OPENGL_ES2=0",
        "SDL_VIDEO_OPENGL_EGL=0",
    }

    filter "system:windows"
        systemversion "latest"
        staticruntime "off"

-- ============================================================
-- imgui
-- ============================================================
project "imgui"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir    ("bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "third_party/imgui/imgui.h",
        "third_party/imgui/imgui.cpp",
        "third_party/imgui/imgui_demo.cpp",
        "third_party/imgui/imgui_draw.cpp",
        "third_party/imgui/imgui_widgets.cpp",
        "third_party/imgui/imgui_tables.cpp",
        "third_party/imgui/imgui_impl_vulkan.cpp",
        "third_party/imgui/imgui_impl_sdl2.cpp",
    }

    includedirs {
        "third_party/imgui",
        "third_party/SDL/include",
        VULKAN_SDK .. "/Include",
    }

    filter "system:windows"
        systemversion "latest"
        staticruntime "off"

-- ============================================================
-- engine
-- ============================================================
project "engine"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    targetdir "bin"
    objdir    ("bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "src/main.cpp",
        "src/vk_types.h",
        "src/vk_initializers.cpp",
        "src/vk_initializers.h",
        "src/vk_images.h",
        "src/vk_images.cpp",
        "src/vk_descriptors.h",
        "src/vk_descriptors.cpp",
        "src/vk_pipelines.h",
        "src/vk_pipelines.cpp",
        "src/vk_engine.h",
        "src/vk_engine.cpp",
        "src/camera.cpp",
        "src/camera.h",
        "src/utils.h",
    }

    includedirs {
        "src",
        "third_party/vkbootstrap",
        "third_party/glm",
        "third_party/vma",
        "third_party/stb_image",
        "third_party/fmt/include",
        "third_party/imgui",
        "third_party/SDL/include",
        VULKAN_SDK .. "/Include",
    }

    libdirs { VULKAN_SDK .. "/Lib" }

    links {
        "vkbootstrap",
        "fmt",
        "imgui",
        "SDL2",
        "vulkan-1",
    }

    defines { "GLM_FORCE_DEPTH_ZERO_TO_ONE", "SDL_STATIC", "SDL_STATIC_LIB" }

-- Shader compilation (glslangValidator from VULKAN_SDK)
    local shaderdir = path.getabsolute("shaders")
    local glslang   = (VULKAN_SDK or "") .. "/Bin/glslangValidator.exe"

    for _, f in ipairs(os.matchfiles("shaders/*.vert")) do
        local spv = f .. ".spv"
        filter {}
        buildmessage ("Compiling shader: " .. f)
        buildcommands  { '"' .. glslang .. '" -V "' .. path.getabsolute(f) .. '" -o "' .. path.getabsolute(spv) .. '"' }
        buildoutputs   { spv }
    end
    for _, f in ipairs(os.matchfiles("shaders/*.frag")) do
        local spv = f .. ".spv"
        filter {}
        buildmessage ("Compiling shader: " .. f)
        buildcommands  { '"' .. glslang .. '" -V "' .. path.getabsolute(f) .. '" -o "' .. path.getabsolute(spv) .. '"' }
        buildoutputs   { spv }
    end
    for _, f in ipairs(os.matchfiles("shaders/*.comp")) do
        local spv = f .. ".spv"
        filter {}
        buildmessage ("Compiling shader: " .. f)
        buildcommands  { '"' .. glslang .. '" -V "' .. path.getabsolute(f) .. '" -o "' .. path.getabsolute(spv) .. '"' }
        buildoutputs   { spv }
    end

    filter "system:windows"
        systemversion "latest"
        staticruntime "off"
        links { "user32", "gdi32", "winmm", "imm32", "ole32", "oleaut32", "shell32", "setupapi", "version", "uuid" }
