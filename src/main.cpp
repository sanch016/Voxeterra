#include "Camera.h"
#include "Player.h"
#include "World.h"
#include "Block.h"
#include "Chunk.h"

#include <raylib.h>
#include <raymath.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cstring>
#include <iostream>
#include <fstream>

static std::ofstream g_log;

#define LOG(msg) do { g_log << msg << std::endl; g_log.flush(); std::cout << msg << std::endl; } while(0)

static const char* VERTEX_SHADER =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform vec3 viewPos;\n"
    "\n"
    "out vec3 fragWorldPos;\n"
    "out vec3 fragNormal;\n"
    "out vec4 fragColor;\n"
    "out float fragDist;\n"
    "\n"
    "void main() {\n"
    "    vec4 worldPos = matModel * vec4(vertexPosition, 1.0);\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "    fragWorldPos = worldPos.xyz;\n"
    "    fragNormal = mat3(matModel) * vertexNormal;\n"
    "    fragColor = vertexColor;\n"
    "    fragDist = length(viewPos - worldPos.xyz);\n"
    "}\n";

static const char* FRAGMENT_SHADER =
    "#version 330\n"
    "\n"
    "in vec3 fragWorldPos;\n"
    "in vec3 fragNormal;\n"
    "in vec4 fragColor;\n"
    "in float fragDist;\n"
    "\n"
    "out vec4 finalColor;\n"
    "\n"
    "uniform vec3 sunDir;\n"
    "uniform vec3 sunColor;\n"
    "uniform vec3 ambientColor;\n"
    "void main() {\n"
    "    vec3 N = normalize(fragNormal);\n"
    "    vec3 L = normalize(sunDir);\n"
    "\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    float ambient = 0.45;\n"
    "\n"
    "    vec3 baseColor = fragColor.rgb;\n"
    "    vec3 lit = baseColor * (ambientColor * ambient + sunColor * diff);\n"
    "\n"
    "    finalColor = vec4(lit, 1.0);\n"
    "}\n";

static Matrix glmToRaylib(const glm::mat4& m) {
    Matrix r;
    std::memcpy(&r, &m, sizeof(Matrix));
    return r;
}

struct Frustum {
    float p[6][4]; // (a, b, c, d) — ax+by+cz+d >= 0 means inside

    void setup(Vector3 pos, Vector3 target, Vector3 up, float fovYDeg, float aspect, float nearP, float farP) {
        Vector3 f = Vector3Normalize(Vector3Subtract(target, pos));
        Vector3 r = Vector3Normalize(Vector3CrossProduct(f, up));
        Vector3 u = Vector3CrossProduct(r, f);

        float halfY = fovYDeg * DEG2RAD * 0.5f;
        float halfX = atanf(tanf(halfY) * aspect);
        float cy = cosf(halfY), sy = sinf(halfY);
        float cx = cosf(halfX), sx = sinf(halfX);

        // Near
        p[0][0] = f.x;  p[0][1] = f.y;  p[0][2] = f.z;
        p[0][3] = -(f.x*pos.x + f.y*pos.y + f.z*pos.z) - nearP;
        // Far
        p[1][0] = -f.x; p[1][1] = -f.y; p[1][2] = -f.z;
        p[1][3] = (f.x*pos.x + f.y*pos.y + f.z*pos.z) + farP;
        // Left
        p[2][0] = cx*r.x + sx*f.x; p[2][1] = cx*r.y + sx*f.y; p[2][2] = cx*r.z + sx*f.z;
        p[2][3] = -(p[2][0]*pos.x + p[2][1]*pos.y + p[2][2]*pos.z);
        // Right
        p[3][0] = -cx*r.x + sx*f.x; p[3][1] = -cx*r.y + sx*f.y; p[3][2] = -cx*r.z + sx*f.z;
        p[3][3] = -(p[3][0]*pos.x + p[3][1]*pos.y + p[3][2]*pos.z);
        // Top
        p[4][0] = -cy*u.x + sy*f.x; p[4][1] = -cy*u.y + sy*f.y; p[4][2] = -cy*u.z + sy*f.z;
        p[4][3] = -(p[4][0]*pos.x + p[4][1]*pos.y + p[4][2]*pos.z);
        // Bottom
        p[5][0] = cy*u.x + sy*f.x; p[5][1] = cy*u.y + sy*f.y; p[5][2] = cy*u.z + sy*f.z;
        p[5][3] = -(p[5][0]*pos.x + p[5][1]*pos.y + p[5][2]*pos.z);
    }

    bool testSphere(float cx, float cy, float cz, float radius) const {
        for (int i = 0; i < 6; ++i) {
            float d = p[i][0]*cx + p[i][1]*cy + p[i][2]*cz + p[i][3];
            if (d < -radius) return false;
        }
        return true;
    }
};

int main() {
    g_log.open("debug_log.txt");
    LOG("=== Starting Voxeterra (raylib) ===");

    const int screenWidth = 1280;
    const int screenHeight = 720;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Voxeterra");
    DisableCursor();

    Shader chunkShader = LoadShaderFromMemory(VERTEX_SHADER, FRAGMENT_SHADER);

    int sunDirLoc       = GetShaderLocation(chunkShader, "sunDir");
    int sunColorLoc     = GetShaderLocation(chunkShader, "sunColor");
    int ambientColorLoc = GetShaderLocation(chunkShader, "ambientColor");

    int matModelLoc     = GetShaderLocation(chunkShader, "matModel");
    int viewPosLoc      = GetShaderLocation(chunkShader, "viewPos");

    float sunDir[3]       = { 0.4f, 0.8f, 0.3f };
    float sunColor[3]     = { 1.0f, 0.95f, 0.85f };
    float ambientColor[3] = { 0.6f, 0.7f, 0.9f };


    SetShaderValue(chunkShader, sunDirLoc, sunDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(chunkShader, sunColorLoc, sunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(chunkShader, ambientColorLoc, ambientColor, SHADER_UNIFORM_VEC3);


    Material chunkMaterial = LoadMaterialDefault();
    chunkMaterial.shader = chunkShader;

    FPSCamera camera(glm::vec3(0.0f, 20.0f, 0.0f));
    Player player(glm::vec3(0.0f, 20.0f, 0.0f));
    World world(4);

    bool showUI = false;
    TerrainParams& tp = world.terrainParams();

    LOG("=== Voxeterra Engine initialized ===");
    LOG("Controls: WASD + Space(up) + Ctrl(down) + Shift(sprint) + Mouse + Tab(UI)");

    auto lastTime = std::chrono::high_resolution_clock::now();
    int frameCount = 0;

    while (!WindowShouldClose()) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        if (IsKeyPressed(KEY_TAB)) {
            showUI = !showUI;
            if (showUI) EnableCursor();
            else DisableCursor();
        }

        if (!showUI) {
            Vector2 mouseDelta = GetMouseDelta();
            camera.processMouse(mouseDelta.x, -mouseDelta.y);
        }

        camera.processKeyboard(deltaTime);
        player.processInput(deltaTime, camera.getYaw());
        player.update(deltaTime, world);
        camera.setPosition(player.getEyePosition());

        world.update(player.getPosition());

        if (frameCount % 120 == 0) {
            auto p = player.getPosition();
            LOG("pos=" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z)
                + " pending=" + std::to_string(world.getPendingCount())
                + " queued=" + std::to_string(world.getQueuedCount())
                + " chunks=" + std::to_string(world.getMeshedCount())
                + " noMesh=" + std::to_string(world.getWithoutMeshCount()));
        }

        Vector3 camPos = {camera.getPosition().x, camera.getPosition().y, camera.getPosition().z};
        SetShaderValue(chunkShader, viewPosLoc, &camPos, SHADER_UNIFORM_VEC3);

        BeginDrawing();
            ClearBackground({158, 186, 226, 255});

            Camera3D rlCamera;
            rlCamera.position = camPos;
            rlCamera.target   = {camera.getPosition().x + camera.getFront().x,
                                 camera.getPosition().y + camera.getFront().y,
                                 camera.getPosition().z + camera.getFront().z};
            rlCamera.up       = {0.0f, 1.0f, 0.0f};
            rlCamera.fovy     = 70.0f;
            rlCamera.projection = CAMERA_PERSPECTIVE;

            BeginMode3D(rlCamera);

            Frustum frustum;
            float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();
            frustum.setup(rlCamera.position, rlCamera.target, rlCamera.up,
                          rlCamera.fovy, aspect, 0.1f, 1000.0f);

            for (Chunk* chunk : world.getChunksToRender()) {
                if (!chunk->hasMesh()) continue;

                float cx = (float)chunk->getChunkX() * Chunk::SIZE * BLOCK_SIZE;
                float cy = (float)chunk->getChunkY() * Chunk::SIZE * BLOCK_SIZE;
                float cz = (float)chunk->getChunkZ() * Chunk::SIZE * BLOCK_SIZE;

                float chunkHalf = Chunk::SIZE * BLOCK_SIZE * 0.5f;
                float chunkRadius = chunkHalf * 1.74f;
                if (!frustum.testSphere(cx + chunkHalf, cy + chunkHalf, cz + chunkHalf, chunkRadius))
                    continue;

                Matrix model = MatrixTranslate(cx, cy, cz);

                SetShaderValueMatrix(chunkShader, matModelLoc, model);
                DrawMesh(chunk->getMesh(), chunkMaterial, model);
            }

            EndMode3D();

            DrawFPS(10, 10);

            if (showUI) {
                int panelX = 10, panelY = 10;
                int panelW = 380;
                int lineH = 20;
                int sliderH = 14;
                int sliderW = 180;
                int py = panelY;

                int totalLines = 3 + 5 + 1 + 6 + 1 + 3 + 1 + 5 + 1 + 8;
                int panelH = totalLines * lineH + 20;
                DrawRectangle(panelX - 4, panelY - 4, panelW + 8, panelH + 8, {0, 0, 0, 180});

                auto sliderF = [&](const char* label, float& val, float lo, float hi, float step) -> bool {
                    int sx = panelX + 120;
                    int sw = sliderW;
                    float t = (hi > lo) ? (val - lo) / (hi - lo) : 0.0f;
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;

                    DrawText(label, panelX, py + 1, 14, WHITE);
                    DrawText(TextFormat("%.3f", val), sx + sw + 8, py + 1, 14, LIGHTGRAY);

                    DrawRectangle(sx, py + 2, sw, sliderH, {60, 60, 60, 255});
                    int handleX = sx + (int)(t * (sw - 8));
                    DrawRectangle(handleX, py, 8, sliderH + 4, {180, 180, 180, 255});

                    bool changed = false;
                    Vector2 mp = GetMousePosition();
                    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                        mp.x >= sx && mp.x <= sx + sw &&
                        mp.y >= py - 4 && mp.y <= py + sliderH + 6) {
                        float newT = (mp.x - (float)sx) / (float)sw;
                        float newVal = lo + newT * (hi - lo);
                        newVal = roundf(newVal / step) * step;
                        if (newVal < lo) newVal = lo;
                        if (newVal > hi) newVal = hi;
                        if (newVal != val) {
                            val = newVal;
                            changed = true;
                        }
                    }

                    py += lineH;
                    return changed;
                };

                auto sliderI = [&](const char* label, int& val, int lo, int hi, int step) -> bool {
                    float fv = static_cast<float>(val);
                    bool changed = sliderF(label, fv, static_cast<float>(lo), static_cast<float>(hi), static_cast<float>(step));
                    if (changed) val = static_cast<int>(fv);
                    return changed;
                };

                DrawText("=== ENGINE ===", panelX, py, 14, YELLOW);
                py += lineH;

                bool terrainDirty = false;

                int seed = tp.seed;
                if (sliderI("Seed", seed, 0, 99999, 1)) {
                    tp.seed = seed;
                    terrainDirty = true;
                }

                int rd = world.getRenderDistance();
                if (sliderI("Render Dist", rd, 4, 64, 1)) {
                    world.setRenderDistance(rd);
                }

                int vr = world.getVerticalRange();
                if (sliderI("Vert Range", vr, 1, 16, 1)) {
                    world.setVerticalRange(vr);
                }

                int cpf = world.getChunksPerFrame();
                if (sliderI("Chunks/Frame", cpf, 1, 128, 1)) {
                    world.setChunksPerFrame(cpf);
                }

                py += 4;
                DrawText("=== HEIGHT ===", panelX, py, 14, YELLOW);
                py += lineH;
                terrainDirty |= sliderI("Sea Level", tp.seaLevel, 0, 20, 1);
                terrainDirty |= sliderI("Min Height", tp.minTerrainHeight, 0, 30, 1);
                terrainDirty |= sliderI("Max Height", tp.maxTerrainHeight, 10, 80, 2);
                terrainDirty |= sliderI("Dirt Depth", tp.dirtDepth, 1, 8, 1);
                terrainDirty |= sliderI("Bedrock D", tp.bedrockDepth, 0, 5, 1);

                py += 4;
                DrawText("=== NOISE ===", panelX, py, 14, YELLOW);
                py += lineH;
                terrainDirty |= sliderF("Cont. Freq", tp.continentalFreq, 0.001f, 0.02f, 0.001f);
                terrainDirty |= sliderF("Cont. Weight", tp.continentalWeight, 0.0f, 1.0f, 0.05f);
                terrainDirty |= sliderF("Eros. Freq", tp.erosionFreq, 0.001f, 0.03f, 0.001f);
                terrainDirty |= sliderF("Eros. Weight", tp.erosionWeight, 0.0f, 1.0f, 0.05f);
                terrainDirty |= sliderF("Peaks Freq", tp.peaksFreq, 0.005f, 0.1f, 0.005f);
                terrainDirty |= sliderF("Peaks Weight", tp.peaksWeight, 0.0f, 1.0f, 0.05f);

                py += 4;
                DrawText("=== BIOME ===", panelX, py, 14, YELLOW);
                py += lineH;
                terrainDirty |= sliderF("Temp Freq", tp.temperatureFreq, 0.001f, 0.01f, 0.001f);
                terrainDirty |= sliderF("Moist Freq", tp.moistureFreq, 0.001f, 0.02f, 0.001f);
                terrainDirty |= sliderF("Snow Line", tp.snowLine, 20.0f, 60.0f, 2.0f);

                py += 4;
                DrawText("=== CAVES / DETAIL ===", panelX, py, 14, YELLOW);
                py += lineH;
                terrainDirty |= sliderF("Cave Freq", tp.caveFreq, 0.01f, 0.1f, 0.005f);
                terrainDirty |= sliderF("Cave Thresh", tp.caveThreshold, 0.4f, 0.8f, 0.02f);
                terrainDirty |= sliderF("Beach Range", tp.beachRange, 0.0f, 5.0f, 0.5f);
                terrainDirty |= sliderF("Ore Freq", tp.oreFreq, 0.01f, 0.2f, 0.005f);

                py += 4;
                DrawText("=== STATS ===", panelX, py, 14, YELLOW);
                py += lineH;
                DrawText(TextFormat("Chunks: %d", (int)world.getMeshedCount()), panelX, py, 14, LIGHTGRAY);
                py += lineH;
                DrawText(TextFormat("Pending: %d", (int)world.getPendingCount()), panelX, py, 14, LIGHTGRAY);
                py += lineH;
                DrawText(TextFormat("NoMesh: %d", (int)world.getWithoutMeshCount()), panelX, py, 14, LIGHTGRAY);

                if (terrainDirty) {
                    world.applyTerrainParams();
                }
            }

        EndDrawing();

        if (frameCount++ == 0) LOG("First frame rendered successfully");
    }

    LOG("Closing window...");
    CloseWindow();
    g_log.close();
    return EXIT_SUCCESS;
}
