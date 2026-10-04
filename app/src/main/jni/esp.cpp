#include <jni.h>
#include <unistd.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "include/offsets.h"
#include "include/esp.h"

// Memory reader — safe because we're in virtual space
template<typename T>
T Read(uintptr_t addr) {
    T val;
    memcpy(&val, reinterpret_cast<void*>(addr), sizeof(T));
    return val;
}

// Get IL2CPP base at runtime
uintptr_t GetIl2CppBase() {
    if (offsets::libil2cpp_base) return offsets::libil2cpp_base;
    
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libil2cpp.so") && strstr(line, "r-xp")) {
            sscanf(line, "%lx-%*lx", &offsets::libil2cpp_base);
            break;
        }
    }
    fclose(maps);
    return offsets::libil2cpp_base;
}

// World to screen projection
Vector3 W2S(Vector3 world, Matrix4x4 view, Matrix4x4 proj, int sw, int sh) {
    Vector4 clip = proj * view * Vector4(world.x, world.y, world.z, 1.0f);
    if (clip.w < 0.1f) return Vector3(-1, -1, -1); // Behind camera
    
    Vector3 ndc = Vector3(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
    return Vector3(
        (ndc.x + 1.0f) * 0.5f * sw,
        (1.0f - ndc.y) * 0.5f * sh,
        ndc.z
    );
}

// Main ESP loop — called from renderer thread
void ESPThread() {
    uintptr_t base = GetIl2CppBase();
    if (!base) return;
    
    // Get camera matrices
    uintptr_t camera_main = Read<uintptr_t>(base + offsets::Camera::main_static);
    Matrix4x4 viewMatrix = Read<Matrix4x4>(camera_main + offsets::Camera::cameraToWorldMatrix);
    Matrix4x4 projMatrix = GetProjectionMatrix(camera_main); // Helper function
    
    // Iterate player list
    uintptr_t playerMgr = Read<uintptr_t>(base + offsets::GameManager);
    uintptr_t playerList = Read<uintptr_t>(playerMgr + 0x48); // List offset
    
    int playerCount = Read<int>(playerList + 0x18); // List size
    
    for (int i = 0; i < playerCount && i < 64; i++) { // Cap at 64
        uintptr_t player = Read<uintptr_t>(playerList + 0x20 + (i * 8));
        if (!player) continue;
        
        PlayerData pd;
        pd.position = Read<Vector3>(player + offsets::Player::position_offset);
        pd.health = Read<float>(player + offsets::Player::health);
        pd.team = Read<int>(player + offsets::Player::team);
        
        // Skip teammates
        int localTeam = GetLocalTeam();
        if (pd.team == localTeam) continue;
        
        // Project to screen
        Vector3 screen = W2S(pd.position, viewMatrix, projMatrix, screenW, screenH);
        if (screen.x > 0 && screen.y > 0) {
            DrawBox(screen.x, screen.y, 60, 100, pd.health / 100.0f);
            DrawText(std::to_string((int)pd.health), screen.x, screen.y - 10);
        }
    }
}
