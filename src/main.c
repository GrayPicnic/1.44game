/*
 * 1.44MB 게임 콘테스트용 포병(아틸러리) 프로토타입.
 * - 외부 런타임(SDL, DirectX 등) 없이 순수 Win32 API + GDI 소프트웨어 렌더링만 사용.
 * - 정적 링크 + -Os + strip 으로 실행파일을 최대한 작게 유지.
 * - 텍스트 렌더링(비트맵 폰트)은 아직 없어서 각도/파워/HP는 창 제목표시줄에 표시한다.
 * - 궤적 미리보기는 일부러 안 넣었다 — "계산해서 맞추는" 재미가 핵심이라 조준은 눈대중+피드백으로.
 */

#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define GAME_W 640
#define GAME_H 360
#define WINDOW_SCALE 2
#define WINDOW_TITLE "1.44MB Artillery"

#define PI_F 3.14159265f
#define GRAVITY 260.0f
#define MAX_SPEED 480.0f
#define TERRAIN_POINTS 9
#define TANK_GROUND_OFFSET 7

typedef struct {
    float x, y;
    float hp;
    float angle;   /* degrees, 0=수평, 90=수직 (자기 진행방향 기준) */
    float power;   /* 0..100 */
    int facing;    /* +1: 오른쪽으로 발사, -1: 왼쪽으로 발사 */
} Tank;

typedef enum { STATE_PLAYER_AIM, STATE_PROJECTILE, STATE_ENEMY_AIM, STATE_GAMEOVER } GameState;

static uint32_t framebuffer[GAME_W * GAME_H];
static BITMAPINFO bmi;
static BOOL running = TRUE;
static BOOL keys[256];
static HWND g_hwnd;

static int terrainHeight[GAME_W];
static Tank player, enemy;
static GameState state = STATE_PLAYER_AIM;
static BOOL projFromPlayer = TRUE;
static float projX, projY, projVX, projVY;
static float aiThinkTimer = 0.0f;
static int enemyShotsFired = 0;
static int winner = 0; /* 1 = player, 2 = enemy */

static void ClearScreen(uint32_t color) {
    for (int i = 0; i < GAME_W * GAME_H; i++) framebuffer[i] = color;
}

static void PutPixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= GAME_W || y >= GAME_H) return;
    framebuffer[y * GAME_W + x] = color;
}

static void FillPixelRect(int x0, int y0, int w, int h, uint32_t color) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            PutPixel(x, y, color);
}

static void DrawBarrel(Tank *t, uint32_t color) {
    float rad = t->angle * PI_F / 180.0f;
    for (float d = 0.0f; d <= 14.0f; d += 1.0f) {
        int px = (int)(t->x + cosf(rad) * (float)t->facing * d);
        int py = (int)(t->y - sinf(rad) * d);
        PutPixel(px, py, color);
        PutPixel(px, py - 1, color);
    }
}

static float RandRange(float lo, float hi) {
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}

static void GenerateTerrain(void) {
    float ctrl[TERRAIN_POINTS];
    for (int i = 0; i < TERRAIN_POINTS; i++) ctrl[i] = RandRange(190.0f, 300.0f);
    for (int x = 0; x < GAME_W; x++) {
        float t = (float)x / (float)(GAME_W - 1) * (float)(TERRAIN_POINTS - 1);
        int seg = (int)t;
        if (seg >= TERRAIN_POINTS - 1) seg = TERRAIN_POINTS - 2;
        float frac = t - (float)seg;
        float h = ctrl[seg] * (1.0f - frac) + ctrl[seg + 1] * frac;
        terrainHeight[x] = (int)h;
    }
}

static void CarveCrater(int cx, int cy) {
    const int radius = 26;
    for (int dx = -radius; dx <= radius; dx++) {
        int nx = cx + dx;
        if (nx < 0 || nx >= GAME_W) continue;
        float factor = 1.0f - (fabsf((float)dx) / (float)radius);
        if (factor < 0.0f) factor = 0.0f;
        int dug = cy + (int)(22.0f * factor);
        if (dug > terrainHeight[nx]) terrainHeight[nx] = dug;
        if (terrainHeight[nx] > GAME_H) terrainHeight[nx] = GAME_H;
    }
}

static void ApplySplashDamage(float ix, float iy) {
    const float splashRadius = 34.0f;
    const float maxDamage = 34.0f;
    Tank *tanks[2] = { &player, &enemy };
    for (int i = 0; i < 2; i++) {
        float dx = tanks[i]->x - ix;
        float dy = tanks[i]->y - iy;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < splashRadius) {
            float dmg = maxDamage * (1.0f - dist / splashRadius);
            tanks[i]->hp -= dmg;
            if (tanks[i]->hp < 0.0f) tanks[i]->hp = 0.0f;
        }
    }
}

static void FireTank(Tank *t, BOOL fromPlayer) {
    float rad = t->angle * PI_F / 180.0f;
    float speed = t->power / 100.0f * MAX_SPEED;
    projX = t->x + (float)t->facing * 10.0f;
    projY = t->y - 8.0f;
    projVX = cosf(rad) * speed * (float)t->facing;
    projVY = -sinf(rad) * speed;
    projFromPlayer = fromPlayer;
    state = STATE_PROJECTILE;
}

static void EnemyTakeAim(void) {
    float distance = fabsf(player.x - enemy.x);
    float noiseDeg = 9.0f - (float)enemyShotsFired * 1.2f;
    if (noiseDeg < 2.0f) noiseDeg = 2.0f;
    float angleDeg = 45.0f + RandRange(-noiseDeg, noiseDeg);
    if (angleDeg < 8.0f) angleDeg = 8.0f;
    if (angleDeg > 82.0f) angleDeg = 82.0f;

    float speed = sqrtf(distance * GRAVITY);
    float noisePct = 0.09f - (float)enemyShotsFired * 0.01f;
    if (noisePct < 0.02f) noisePct = 0.02f;
    speed *= (1.0f + RandRange(-noisePct, noisePct));

    float power = speed / MAX_SPEED * 100.0f;
    if (power < 10.0f) power = 10.0f;
    if (power > 100.0f) power = 100.0f;

    enemy.angle = angleDeg;
    enemy.power = power;
    enemyShotsFired++;
}

static void ResetGame(void) {
    GenerateTerrain();
    player.x = 70.0f; player.facing = 1; player.angle = 45.0f; player.power = 55.0f; player.hp = 100.0f;
    enemy.x = GAME_W - 70.0f; enemy.facing = -1; enemy.angle = 45.0f; enemy.power = 55.0f; enemy.hp = 100.0f;
    player.y = (float)terrainHeight[(int)player.x] - TANK_GROUND_OFFSET;
    enemy.y = (float)terrainHeight[(int)enemy.x] - TANK_GROUND_OFFSET;
    state = STATE_PLAYER_AIM;
    aiThinkTimer = 0.0f;
    enemyShotsFired = 0;
    winner = 0;
}

static void UpdateTitle(void) {
    char buf[200];
    switch (state) {
        case STATE_PLAYER_AIM:
            wsprintfA(buf, "%s  |  각도 %d\xB0  \xB7  \xED\x8C\x8C\xEC\x9B\x8C %d%%  \xB7  Space \xEB\xB0\x9C\xEC\x82\xAC  |  P1 HP %d  ENEMY HP %d",
                WINDOW_TITLE, (int)player.angle, (int)player.power, (int)player.hp, (int)enemy.hp);
            break;
        case STATE_ENEMY_AIM:
            wsprintfA(buf, "%s  |  \xEC\x83\x81\xEB\x8C\x80 \xEC\xA1\xB0\xEC\xA4\x80 \xEC\xA4\x91...  |  P1 HP %d  ENEMY HP %d",
                WINDOW_TITLE, (int)player.hp, (int)enemy.hp);
            break;
        case STATE_PROJECTILE:
            wsprintfA(buf, "%s  |  P1 HP %d  ENEMY HP %d", WINDOW_TITLE, (int)player.hp, (int)enemy.hp);
            break;
        case STATE_GAMEOVER:
            wsprintfA(buf, "%s  |  %s \xEC\x8A\xB9\xEB\xA6\xAC! R\xEB\xA1\x9C \xEC\x9E\xAC\xEC\x8B\x9C\xEC\x9E\x91",
                WINDOW_TITLE, winner == 1 ? "PLAYER" : "ENEMY");
            break;
    }
    SetWindowTextA(g_hwnd, buf);
}

static void UpdateGame(float dt) {
    static float titleTimer = 0.0f;

    if (state == STATE_GAMEOVER) {
        if (keys[VK_RETURN] || keys['R']) ResetGame();
        titleTimer += dt;
        if (titleTimer > 0.2f) { titleTimer = 0.0f; UpdateTitle(); }
        return;
    }

    if (state == STATE_PLAYER_AIM) {
        float angleRate = 45.0f, powerRate = 60.0f;
        if (keys[VK_UP]    || keys['W']) player.angle += angleRate * dt;
        if (keys[VK_DOWN]  || keys['S']) player.angle -= angleRate * dt;
        if (keys[VK_RIGHT] || keys['D']) player.power += powerRate * dt;
        if (keys[VK_LEFT]  || keys['A']) player.power -= powerRate * dt;
        if (player.angle < 5.0f) player.angle = 5.0f;
        if (player.angle > 89.0f) player.angle = 89.0f;
        if (player.power < 10.0f) player.power = 10.0f;
        if (player.power > 100.0f) player.power = 100.0f;
        if (keys[VK_SPACE]) FireTank(&player, TRUE);
    } else if (state == STATE_ENEMY_AIM) {
        if (aiThinkTimer <= 0.0f) {
            EnemyTakeAim();
            aiThinkTimer = 0.7f;
        }
        aiThinkTimer -= dt;
        if (aiThinkTimer <= 0.0f) FireTank(&enemy, FALSE);
    } else if (state == STATE_PROJECTILE) {
        projVY += GRAVITY * dt;
        projX += projVX * dt;
        projY += projVY * dt;

        BOOL impact = FALSE;
        int ix = (int)projX;
        if (ix < 0 || ix >= GAME_W) {
            impact = TRUE;
        } else if (projY >= (float)terrainHeight[ix]) {
            impact = TRUE;
        } else {
            float dxp = player.x - projX, dyp = player.y - projY;
            float dxe = enemy.x - projX, dye = enemy.y - projY;
            if (sqrtf(dxp * dxp + dyp * dyp) < 12.0f) impact = TRUE;
            if (sqrtf(dxe * dxe + dye * dye) < 12.0f) impact = TRUE;
        }

        if (impact) {
            int cx = ix < 0 ? 0 : (ix >= GAME_W ? GAME_W - 1 : ix);
            CarveCrater(cx, (int)projY);
            ApplySplashDamage(projX, projY);

            if (player.hp <= 0.0f || enemy.hp <= 0.0f) {
                winner = (player.hp <= 0.0f && enemy.hp <= 0.0f) ? (projFromPlayer ? 1 : 2)
                         : (player.hp <= 0.0f ? 2 : 1);
                state = STATE_GAMEOVER;
            } else {
                state = projFromPlayer ? STATE_ENEMY_AIM : STATE_PLAYER_AIM;
                aiThinkTimer = 0.0f;
            }
        }
    }

    /* 크레이터로 파인 지형에 맞춰 탱크 위치 갱신 */
    player.y = (float)terrainHeight[(int)player.x] - TANK_GROUND_OFFSET;
    enemy.y = (float)terrainHeight[(int)enemy.x] - TANK_GROUND_OFFSET;

    titleTimer += dt;
    if (titleTimer > 0.15f) { titleTimer = 0.0f; UpdateTitle(); }
}

static void RenderGame(void) {
    ClearScreen(0xFF10142A);

    for (int x = 0; x < GAME_W; x++) {
        int h = terrainHeight[x];
        FillPixelRect(x, h, 1, GAME_H - h, 0xFF4A6B3C);
        PutPixel(x, h, 0xFF7FA35C);
    }

    if (player.hp > 0.0f) {
        FillPixelRect((int)player.x - 9, (int)player.y - 5, 18, 10, 0xFF4AD9E0);
        DrawBarrel(&player, 0xFFEAF7F8);
    }
    if (enemy.hp > 0.0f) {
        FillPixelRect((int)enemy.x - 9, (int)enemy.y - 5, 18, 10, 0xFFE0563F);
        DrawBarrel(&enemy, 0xFFF8D9D3);
    }

    if (state == STATE_PROJECTILE) {
        FillPixelRect((int)projX - 2, (int)projY - 2, 4, 4, 0xFFF7E36B);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DESTROY:
            running = FALSE;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) running = FALSE;
            if (wp < 256) keys[wp] = TRUE;
            return 0;
        case WM_KEYUP:
            if (wp < 256) keys[wp] = FALSE;
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)hPrev; (void)cmdLine;

    srand(GetTickCount());

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "GameWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);

    RECT r = {0, 0, GAME_W * WINDOW_SCALE, GAME_H * WINDOW_SCALE};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowA(
        "GameWindowClass", WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        NULL, NULL, hInst, NULL);
    g_hwnd = hwnd;
    ShowWindow(hwnd, nShow);

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = GAME_W;
    bmi.bmiHeader.biHeight = -GAME_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(hwnd);

    ResetGame();

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    MSG msg;
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = FALSE;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - prev.QuadPart) / (float)freq.QuadPart;
        prev = now;
        if (dt > 0.05f) dt = 0.05f;

        UpdateGame(dt);
        RenderGame();

        RECT client;
        GetClientRect(hwnd, &client);
        StretchDIBits(hdc,
            0, 0, client.right, client.bottom,
            0, 0, GAME_W, GAME_H,
            framebuffer, &bmi, DIB_RGB_COLORS, SRCCOPY);

        Sleep(1);
    }

    ReleaseDC(hwnd, hdc);
    return 0;
}
