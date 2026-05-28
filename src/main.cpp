#ifdef _WIN32
// Force the system to use high-performance dedicated GPU (NVIDIA / AMD) if available, with automatic integrated fallback
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "camera/Camera.hpp"
#include "voxel/World.hpp"
#include <string>
#include <memory>
#include <random>

// Helper to draw a beautiful, premium modern button with smooth hover reactions
bool DrawButton(Rectangle rect, const char* text, Color baseBg, Color hoverBg, Color borderCol, Color textCol) {
    Vector2 mousePos = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mousePos, rect);
    bool clicked = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    // Draw glassmorphism background
    DrawRectangleRounded(rect, 0.25f, 4, hovered ? hoverBg : baseBg);
    
    // Draw thin glossy border
    DrawRectangleRoundedLines(rect, 0.25f, 4, 1.0f, borderCol);

    // Draw text centered
    int fontSize = 16;
    int textWidth = MeasureText(text, fontSize);
    int posX = static_cast<int>(rect.x + (rect.width - textWidth) / 2);
    int posY = static_cast<int>(rect.y + (rect.height - fontSize) / 2);
    
    DrawText(text, posX, posY, fontSize, textCol);

    return clicked;
}

// Helper to draw a beautiful custom glassmorphism slider with live value readout
bool DrawSlider(Rectangle rect, const char* label, float& value, float minVal, float maxVal, Color barCol, Color knobCol, Color textCol) {
    // Draw label with current value
    int fontSize = 14;
    std::string valStr = std::to_string(static_cast<int>(value));
    std::string fullLabel = std::string(label) + ": " + valStr;
    DrawText(fullLabel.c_str(), static_cast<int>(rect.x), static_cast<int>(rect.y - 18), fontSize, textCol);

    // Draw slider bar background
    Rectangle barBg = { rect.x, rect.y + rect.height / 2 - 3, rect.width, 6 };
    DrawRectangleRounded(barBg, 0.5f, 4, barCol);

    // Calculate knob position based on value
    float percent = (value - minVal) / (maxVal - minVal);
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;

    float knobX = rect.x + percent * rect.width;
    float knobY = rect.y + rect.height / 2;
    float knobRadius = 7.0f;

    // Draw knob
    DrawCircle(static_cast<int>(knobX), static_cast<int>(knobY), knobRadius, knobCol);
    DrawCircleLines(static_cast<int>(knobX), static_cast<int>(knobY), knobRadius + 1.0f, ColorAlpha(WHITE, 0.4f));

    // Handle mouse drag input
    Vector2 mousePos = GetMousePosition();
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Rectangle interactionRect = { rect.x - 10, rect.y - 5, rect.width + 20, rect.height + 10 };
        if (CheckCollisionPointRec(mousePos, interactionRect)) {
            float t = (mousePos.x - rect.x) / rect.width;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            value = minVal + t * (maxVal - minVal);
            return true;
        }
    }

    return false;
}

int main() {
    // -------------------------------------------------------------
    // GLSL Shaders: Pure GPU unpacking of 32-bit packed vertices
    // -------------------------------------------------------------
    const char* vertexShaderSource = 
        "#version 330 core\n"
        "layout (location = 0) in uint inPackedData;\n"
        "out vec3 fragWorldPos;\n"
        "out vec3 fragNormal;\n"
        "out vec2 fragUV;\n"
        "flat out uint fragTextureId;\n"
        "out float fragAO;\n"
        "uniform mat4 mvp;\n"
        "uniform float fadeProgress;\n"
        "const vec3 NORMALS[6] = vec3[6](\n"
        "    vec3(-1.0,  0.0,  0.0),\n" // 0: -X
        "    vec3( 1.0,  0.0,  0.0),\n" // 1: +X
        "    vec3( 0.0, -1.0,  0.0),\n" // 2: -Y
        "    vec3( 0.0,  1.0,  0.0),\n" // 3: +Y
        "    vec3( 0.0,  0.0, -1.0),\n" // 4: -Z
        "    vec3( 0.0,  0.0,  1.0)\n"  // 5: +Z
        ");\n"
        "const vec2 UVS[4] = vec2[4](\n"
        "    vec2(0.0, 0.0),\n"
        "    vec2(1.0, 0.0),\n"
        "    vec2(1.0, 1.0),\n"
        "    vec2(0.0, 1.0)\n"
        ");\n"
        "void main() {\n"
        "    float x = float(inPackedData & 0x3Fu);\n"
        "    float y = float((inPackedData >> 6u) & 0x1FFu);\n"
        "    float z = float((inPackedData >> 15u) & 0x3Fu);\n"
        "    uint normalIdx = (inPackedData >> 21u) & 0x7u;\n"
        "    uint cornerIdx = (inPackedData >> 24u) & 0x3u;\n"
        "    uint textureId = (inPackedData >> 26u) & 0xFu;\n"
        "    uint ao = (inPackedData >> 30u) & 0x3u;\n"
        "    float offsetY = -48.0 * (1.0 - fadeProgress) * (1.0 - fadeProgress);\n"
        "    fragWorldPos = vec3(x, y + offsetY, z);\n"
        "    fragNormal = NORMALS[normalIdx];\n"
        "    fragUV = UVS[cornerIdx];\n"
        "    fragTextureId = textureId;\n"
        "    fragAO = 0.45 + (float(ao) / 3.0) * 0.55;\n"
        "    gl_Position = mvp * vec4(fragWorldPos, 1.0);\n"
        "}\n";

    const char* fragmentShaderSource = 
        "#version 330 core\n"
        "in vec3 fragWorldPos;\n"
        "in vec3 fragNormal;\n"
        "in vec2 fragUV;\n"
        "flat in uint fragTextureId;\n"
        "in float fragAO;\n"
        "out vec4 finalColor;\n"
        "uniform float fadeProgress;\n"
        "const vec4 BLOCK_COLORS[9] = vec4[9](\n"
        "    vec4(0.0, 0.0, 0.0, 0.0),\n"
        "    vec4(0.18, 0.72, 0.45, 1.0),\n"
        "    vec4(0.45, 0.30, 0.22, 1.0),\n"
        "    vec4(0.42, 0.48, 0.54, 1.0),\n"
        "    vec4(0.90, 0.80, 0.57, 1.0),\n"
        "    vec4(0.95, 0.96, 0.97, 1.0),\n"
        "    vec4(0.35, 0.24, 0.18, 1.0),\n"
        "    vec4(0.15, 0.55, 0.33, 1.0),\n"
        "    vec4(0.16, 0.50, 0.73, 0.70)\n"
        ");\n"
        "float getShading(vec3 normal) {\n"
        "    if (normal.y > 0.1) return 1.0;\n"
        "    if (normal.y < -0.1) return 0.5;\n"
        "    if (abs(normal.x) > 0.1) return 0.8;\n"
        "    return 0.9;\n"
        "}\n"
        "void main() {\n"
        "    uint type = fragTextureId;\n"
        "    if (type >= 9u) {\n"
        "        finalColor = vec4(1.0, 0.0, 1.0, 1.0);\n"
        "        return;\n"
        "    }\n"
        "    vec4 baseColor = BLOCK_COLORS[type];\n"
        "    float light = getShading(fragNormal);\n"
        "    finalColor = vec4(baseColor.rgb * light * fragAO, baseColor.a);\n"
        "}\n";

    // -------------------------------------------------------------
    // 2. Initializations
    // -------------------------------------------------------------
    const int screenWidth = 1280;
    const int screenHeight = 720;

    // Enable multi-sampling (anti-aliasing) and resizable window for high visual quality
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Procedural Voxel Engine - AntiGravity");
    SetTargetFPS(120); // Smooth visual rate locked at 120 FPS max

    // Compile GLSL shaders from memory
    Shader voxelShader = LoadShaderFromMemory(vertexShaderSource, fragmentShaderSource);

    // Instantiate systems
    auto world = std::make_unique<World>();
    
    // Generate initial world with seed 1337
    int seedValue = 1337;
    world->recreate(seedValue);

    // Position player safely above grand ground level (surface is around y=130)
    auto flyCam = std::make_unique<FlyCamera>(Vector3{ 16.0f, 150.0f, 16.0f });

    // Seed generation state
    std::mt19937 randEngine(std::random_device{}());

    // -------------------------------------------------------------
    // 3. Application Loop
    // -------------------------------------------------------------
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f; // Cap dt to avoid huge jumps on frame drops

        // Update Camera controller
        flyCam->update(dt);

        // Update infinite chunk generator around player's position
        world->update(flyCam->camera.position, dt);

        // -------------------------------------------------------------
        // 4. Rendering
        // -------------------------------------------------------------
        BeginDrawing();
        
        // Premium sky gradient background (Soft sky blue to pastel morning pink)
        ClearBackground(Color{ 197, 223, 242, 255 });
        
        // Draw 3D voxel scene
        BeginMode3D(flyCam->camera);
            
            // Dynamic far clipping plane to support render distance of 64 chunks (up to 3000+ blocks diagonally)
            // Raylib's default is 1000.0f, which clips chunks beyond ~31 chunks distance.
            {
                float farPlane = static_cast<float>(world->renderDistance) * 32.0f * 1.5f;
                if (farPlane < 1000.0f) farPlane = 1000.0f;
                float aspect = static_cast<float>(GetScreenWidth()) / static_cast<float>(GetScreenHeight());
                Matrix customProj = MatrixPerspective(flyCam->camera.fovy * DEG2RAD, aspect, 0.05f, farPlane);
                rlSetMatrixProjection(customProj);
            }

            // Draw world terrain with custom shader (with view cone frustum culling)
            Vector3 camForward = Vector3Normalize(Vector3Subtract(flyCam->camera.target, flyCam->camera.position));
            world->draw(flyCam->camera.position, camForward, voxelShader);

            // Draw a subtle coordinate grid at the sea level to emphasize depth
            DrawGrid(20, 16.0f);

        EndMode3D();

        // -------------------------------------------------------------
        // 4. Stylized Glassmorphism UI Drawing
        // -------------------------------------------------------------
        
        // Draw dark frosted HUD glass panel on the left (height dynamically resizes)
        Rectangle hudPanel = { 20.0f, 20.0f, 320.0f, static_cast<float>(GetScreenHeight()) - 40.0f };
        DrawRectangleRounded(hudPanel, 0.05f, 4, Color{ 20, 24, 33, 210 });
        DrawRectangleRoundedLines(hudPanel, 0.05f, 4, 1.5f, Color{ 255, 255, 255, 35 });

        // Header Title
        DrawText("VOXEL ENGINE", static_cast<int>(hudPanel.x + 25), static_cast<int>(hudPanel.y + 30), 24, Color{ 46, 184, 114, 255 });
        DrawText("Procedural Sandbox", static_cast<int>(hudPanel.x + 25), static_cast<int>(hudPanel.y + 60), 14, Color{ 150, 160, 180, 255 });
        DrawLine(static_cast<int>(hudPanel.x + 25), static_cast<int>(hudPanel.y + 85), static_cast<int>(hudPanel.x + 295), static_cast<int>(hudPanel.y + 85), Color{ 255, 255, 255, 25 });

        // HUD Section 1: Performance Stats
        int statStartY = static_cast<int>(hudPanel.y + 95);
        DrawText("PERFORMANCE STATS", static_cast<int>(hudPanel.x + 25), statStartY, 14, Color{ 110, 130, 160, 255 });
        
        std::string fpsStr = "FPS: " + std::to_string(GetFPS());
        DrawText(fpsStr.c_str(), static_cast<int>(hudPanel.x + 25), statStartY + 25, 16, WHITE);

        std::string chunkStr = "Loaded Chunks: " + std::to_string(world->getChunkCount());
        DrawText(chunkStr.c_str(), static_cast<int>(hudPanel.x + 25), statStartY + 50, 16, WHITE);

        std::string taskStr = "Background Jobs: " + std::to_string(world->activeTasks.load());
        DrawText(taskStr.c_str(), static_cast<int>(hudPanel.x + 25), statStartY + 75, 16, 
                 world->activeTasks.load() > 0 ? Color{ 230, 142, 60, 255 } : Color{ 46, 184, 114, 255 });

        std::string posStr = "Pos: X: " + std::to_string(static_cast<int>(flyCam->camera.position.x)) +
                             " Y: " + std::to_string(static_cast<int>(flyCam->camera.position.y)) +
                             " Z: " + std::to_string(static_cast<int>(flyCam->camera.position.z));
        DrawText(posStr.c_str(), static_cast<int>(hudPanel.x + 25), statStartY + 100, 15, Color{ 200, 210, 230, 255 });

        std::string speedStr = "Fly Speed: " + std::to_string(static_cast<int>(flyCam->maxSpeed)) + " m/s (Scroll)";
        DrawText(speedStr.c_str(), static_cast<int>(hudPanel.x + 25), statStartY + 122, 15, Color{ 46, 184, 114, 255 });

        DrawLine(static_cast<int>(hudPanel.x + 25), static_cast<int>(hudPanel.y + 250), static_cast<int>(hudPanel.x + 295), static_cast<int>(hudPanel.y + 250), Color{ 255, 255, 255, 25 });

        // HUD Section 2: Seed & Customization controls
        int seedStartY = static_cast<int>(hudPanel.y + 265);
        DrawText("WORLD PARAMETERS", static_cast<int>(hudPanel.x + 25), seedStartY, 14, Color{ 110, 130, 160, 255 });

        // Seed Display Panel
        Rectangle seedBox = { hudPanel.x + 25, static_cast<float>(seedStartY + 25), 270.0f, 35.0f };
        DrawRectangleRounded(seedBox, 0.2f, 4, Color{ 10, 12, 16, 180 });
        DrawRectangleRoundedLines(seedBox, 0.2f, 4, 1.0f, Color{ 255, 255, 255, 15 });
        
        std::string curSeedStr = "Active Seed: " + std::to_string(seedValue);
        DrawText(curSeedStr.c_str(), static_cast<int>(seedBox.x + 15), static_cast<int>(seedBox.y + 10), 14, Color{ 46, 184, 114, 255 });

        // Seed Controls: Decrement, Increment, and Randomizer
        Rectangle btnDec = { hudPanel.x + 25, static_cast<float>(seedStartY + 68), 50.0f, 30.0f };
        Rectangle btnInc = { hudPanel.x + 80, static_cast<float>(seedStartY + 68), 50.0f, 30.0f };
        Rectangle btnRand = { hudPanel.x + 135, static_cast<float>(seedStartY + 68), 160.0f, 30.0f };

        if (DrawButton(btnDec, "- 1", Color{ 30, 36, 48, 200 }, Color{ 45, 54, 72, 255 }, Color{ 255, 255, 255, 20 }, WHITE)) {
            seedValue--;
            world->recreate(seedValue);
        }

        if (DrawButton(btnInc, "+ 1", Color{ 30, 36, 48, 200 }, Color{ 45, 54, 72, 255 }, Color{ 255, 255, 255, 20 }, WHITE)) {
            seedValue++;
            world->recreate(seedValue);
        }

        if (DrawButton(btnRand, "Random Seed", Color{ 30, 36, 48, 200 }, Color{ 46, 184, 114, 180 }, Color{ 46, 184, 114, 80 }, WHITE)) {
            std::uniform_int_distribution<int> dist(1, 999999);
            seedValue = dist(randEngine);
            world->recreate(seedValue);
        }

        // View Distance Slider (Throttle updates by committing only on mouse button release)
        static float sliderViewDistance = 16.0f;
        Rectangle sliderRect = { hudPanel.x + 25, static_cast<float>(seedStartY + 130), 270.0f, 15.0f };
        DrawSlider(sliderRect, "View Distance (Chunks)", sliderViewDistance, 3.0f, 64.0f, Color{ 30, 36, 48, 200 }, Color{ 46, 184, 114, 255 }, Color{ 110, 130, 160, 255 });
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            world->renderDistance = static_cast<int>(sliderViewDistance);
        }

        // Recreate World button (styled in signature green)
        Rectangle btnRebuild = { hudPanel.x + 25, static_cast<float>(seedStartY + 165), 270.0f, 40.0f };
        if (DrawButton(btnRebuild, "Recreate Terrain", Color{ 46, 184, 114, 200 }, Color{ 58, 204, 131, 255 }, Color{ 255, 255, 255, 30 }, WHITE)) {
            world->recreate(seedValue);
        }

        DrawLine(static_cast<int>(hudPanel.x + 25), static_cast<int>(hudPanel.y + 490), static_cast<int>(hudPanel.x + 295), static_cast<int>(hudPanel.y + 490), Color{ 255, 255, 255, 25 });

        // HUD Section 3: Navigation Controls Cheat Sheet
        int ctrlStartY = static_cast<int>(hudPanel.y + 505);
        DrawText("CONTROLS CHEAT SHEET", static_cast<int>(hudPanel.x + 25), ctrlStartY, 14, Color{ 110, 130, 160, 255 });
        
        DrawText("HOLD [Right Click] : Look around", static_cast<int>(hudPanel.x + 25), ctrlStartY + 20, 13, Color{ 180, 190, 210, 255 });
        DrawText("W / A / S / D      : Fly movement", static_cast<int>(hudPanel.x + 25), ctrlStartY + 38, 13, Color{ 180, 190, 210, 255 });
        DrawText("SPACE / L-SHIFT    : Fly Up / Down", static_cast<int>(hudPanel.x + 25), ctrlStartY + 56, 13, Color{ 180, 190, 210, 255 });
        DrawText("MOUSE WHEEL        : Adjust Fly Speed", static_cast<int>(hudPanel.x + 25), ctrlStartY + 74, 13, Color{ 46, 184, 114, 255 });
        DrawText("RELEASE [R-Click]  : Release cursor", static_cast<int>(hudPanel.x + 25), ctrlStartY + 92, 13, Color{ 180, 190, 210, 255 });

        // HUD Section 4: App Close button (anchored dynamically to the bottom of the HUD panel)
        float lineAboveY = hudPanel.y + hudPanel.height - 65.0f;
        float btnCloseY = hudPanel.y + hudPanel.height - 50.0f;
        DrawLine(static_cast<int>(hudPanel.x + 25), static_cast<int>(lineAboveY), static_cast<int>(hudPanel.x + 295), static_cast<int>(lineAboveY), Color{ 255, 255, 255, 25 });

        Rectangle btnClose = { hudPanel.x + 25, btnCloseY, 270.0f, 40.0f };
        if (DrawButton(btnClose, "Close Application", Color{ 219, 68, 85, 180 }, Color{ 235, 87, 105, 255 }, Color{ 255, 255, 255, 30 }, WHITE)) {
            break;
        }

        EndDrawing();
    }

    // -------------------------------------------------------------
    // 5. Cleanup
    // -------------------------------------------------------------
    world.reset(); // Safely unload all resources before closing OpenGL context
    UnloadShader(voxelShader);
    CloseWindow();
    
    return 0;
}
