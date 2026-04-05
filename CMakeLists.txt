cmake_minimum_required(VERSION 3.20)
project(PhysiSimCAD LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.6
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.3
)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

set(IMGUI_SOURCES
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SPV_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SPV_DIR})

function(compile_shader src name)
  add_custom_command(
    OUTPUT ${SPV_DIR}/${name}.spv
    COMMAND ${GLSLC} -O ${src} -o ${SPV_DIR}/${name}.spv
    DEPENDS ${src}
    COMMENT "Compiling shader ${name}"
  )
endfunction()

compile_shader(${SHADER_DIR}/mesh.vert mesh_vert)
compile_shader(${SHADER_DIR}/mesh.frag mesh_frag)
compile_shader(${SHADER_DIR}/fea/laplacian_smooth.comp laplacian_smooth)

add_executable(physisim
  src/main.cpp
  src/core/Application.cpp
  src/core/Scene.cpp
  src/core/CommandSystem.cpp
  src/geometry/Mesh.cpp
  src/geometry/MeshOperations.cpp
  src/geometry/GeometryEngine.cpp
  src/rendering/Camera.cpp
  src/rendering/VulkanDevice.cpp
  src/rendering/VulkanPipeline.cpp
  src/rendering/VulkanRenderer.cpp
  src/rendering/RayPick.cpp
  src/ai/LLMClient.cpp
  src/ai/MathClient.cpp
  src/ai/AnalysisClient.cpp
  src/ai/CommandValidator.cpp
  src/ai/AIOrchestrator.cpp
  src/analysis/GeometryAnalyzer.cpp
  src/analysis/HeuristicAnalyzer.cpp
  src/analysis/GroundTruth.cpp
  src/analysis/FeedbackBuilder.cpp
  src/analysis/AnalysisMemory.cpp
  src/analysis/DefectDetector.cpp
  src/analysis/MeshMetrics.cpp
  src/analysis/MeshHighlightMerge.cpp
  src/analysis/MeshBenchmark.cpp
  src/analysis/WeaknessField.cpp
  src/fea/MeshAdjacency.cpp
  src/fea/GpuLaplacianSmooth.cpp
  src/fem/TetrahedralMesh.cpp
  src/fem/FemMeshReadiness.cpp
  src/fem/FemCompare.cpp
  src/fem/calculix/CalculixInputWriter.cpp
  src/fem/calculix/CalculixRunner.cpp
  src/fem/calculix/CalculixOutputParser.cpp
  src/fem/calculix/FemCalculix.cpp
  src/ui/ImGuiLayer.cpp
  src/ui/UiTheme.cpp
  src/ipc/CommandApiServer.cpp
  src/platform/FileDialog.cpp
  ${IMGUI_SOURCES}
  ${SPV_DIR}/mesh_vert.spv
  ${SPV_DIR}/mesh_frag.spv
  ${SPV_DIR}/laplacian_smooth.spv
)

target_include_directories(physisim PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${glm_SOURCE_DIR}
)

target_compile_definitions(physisim PRIVATE
  GLFW_INCLUDE_VULKAN
  IMGUI_IMPL_VULKAN_NO_PROTOTYPES
  GLM_FORCE_RADIANS
  GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_link_libraries(physisim PRIVATE
  glfw
  glm::glm
  Vulkan::Vulkan
  nlohmann_json::nlohmann_json
  httplib::httplib
)

if(WIN32)
  target_link_libraries(physisim PRIVATE ws2_32 crypt32 comdlg32)
endif()

target_compile_definitions(physisim PRIVATE
  PHYSISIM_SHADER_DIR="${SPV_DIR}"
)

# ---------------------------------------------------------------------------
# Vulkan + ImGui remote viewer (HTTP client to physisim --ipc-port)
# ---------------------------------------------------------------------------
add_executable(physisim_client
  src/client/main.cpp
  src/client/ClientApp.cpp
  src/geometry/Mesh.cpp
  src/rendering/Camera.cpp
  src/rendering/VulkanDevice.cpp
  src/rendering/VulkanPipeline.cpp
  src/rendering/VulkanRenderer.cpp
  src/ui/ImGuiLayer.cpp
  src/ui/UiTheme.cpp
  ${IMGUI_SOURCES}
  ${SPV_DIR}/mesh_vert.spv
  ${SPV_DIR}/mesh_frag.spv
)

target_include_directories(physisim_client PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${glm_SOURCE_DIR}
)

target_compile_definitions(physisim_client PRIVATE
  GLFW_INCLUDE_VULKAN
  IMGUI_IMPL_VULKAN_NO_PROTOTYPES
  GLM_FORCE_RADIANS
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  PHYSISIM_SHADER_DIR="${SPV_DIR}"
)

target_link_libraries(physisim_client PRIVATE
  glfw
  glm::glm
  Vulkan::Vulkan
  nlohmann_json::nlohmann_json
  httplib::httplib
)

if(WIN32)
  target_link_libraries(physisim_client PRIVATE ws2_32 crypt32)
endif()
