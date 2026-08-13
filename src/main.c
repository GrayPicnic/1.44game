/*
 * 1.44MB 게임 콘테스트용 웨이브 슈팅 프로토타입.
 * - 외부 런타임(SDL, DirectX 등) 없이 순수 Win32 API + GDI 소프트웨어 렌더링만 사용.
 * - 정적 링크 + -Os + strip 으로 실행파일을 최대한 작게 유지.
 * - 텍스트 렌더링(비트맵 폰트)은 아직 없어서 점수/웨이브는 창 제목표시줄에 표시한다.
 */

#include <windows.h>
#include <stdint.h>
#include <stdlib.h>

#define GAME_W 640
#define GAME_H 360
#define WINDOW_SCALE 2
#define WINDOW_TITLE "1.44MB Game"

#define MAX_BULLETS 64
#define MAX_ENEMIES 40
#define PLAYER_W 16
#define PLAYER_H 14

typedef struct { float x, y; BOOL active; } Bullet;
typedef struct { float x, y, vx; BOOL active; } Enemy;

static uint32_t framebuffer[GAME_W * GAME_H];
static BITMAPINFO bmi;
static BOOL running = TRUE;
static BOOL keys[256];
static HWND g_hwnd;

static float playerX = GAME_W / 2.0f;
static float playerY = GAME_H - 40.0f;

static Bullet bullets[MAX_BULLETS];
static Enemy enemies[MAX_ENEMIES];

static float fireCooldown = 0.0f;
static float spawnTimer = 1.0f;
static float waveTime = 0.0f;
static int score = 0;
static int wave = 1;
static BOOL gameOver = FALSE;

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

static void ResetGame(void) {
    playerX = GAME_W / 2.0f;
    playerY = GAME_H - 40.0f;
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = FALSE;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = FALSE;
    fireCooldown = 0.0f;
    spawnTimer = 1.0f;
    waveTime = 0.0f;
    score = 0;
    wave = 1;
    gameOver = FALSE;
}

static void SpawnEnemy(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) continue;
        enemies[i].active = TRUE;
        enemies[i].x = 20.0f + (float)(rand() % (GAME_W - 40));
        enemies[i].y = -12.0f;
        enemies[i].vx = (float)((rand() % 200) - 100) * 0.3f; /* -30..30 px/s */
        return;
    }
}

static void FireBullet(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) continue;
        bullets[i].active = TRUE;
        bullets[i].x = playerX;
        bullets[i].y = playerY - PLAYER_H / 2.0f;
        return;
    }
}

static void UpdateTitle(void) {
    char buf[160];
    if (gameOver) {
        wsprintfA(buf, "%s  |  GAME OVER - Score %d (Wave %d) - Press R to restart",
                  WINDOW_TITLE, score, wave);
    } else {
        wsprintfA(buf, "%s  |  Score %d  Wave %d", WINDOW_TITLE, score, wave);
    }
    SetWindowTextA(g_hwnd, buf);
}

static void UpdateGame(float dt) {
    static float titleTimer = 0.0f;

    if (gameOver) {
        if (keys[VK_RETURN] || keys['R']) ResetGame();
        titleTimer += dt;
        if (titleTimer > 0.2f) { titleTimer = 0.0f; UpdateTitle(); }
        return;
    }

    float speed = 240.0f;
    if (keys[VK_LEFT]  || keys['A']) playerX -= speed * dt;
    if (keys[VK_RIGHT] || keys['D']) playerX += speed * dt;
    if (keys[VK_UP]    || keys['W']) playerY -= speed * dt;
    if (keys[VK_DOWN]  || keys['S']) playerY += speed * dt;
    if (playerX < PLAYER_W) playerX = PLAYER_W;
    if (playerX > GAME_W - PLAYER_W) playerX = GAME_W - PLAYER_W;
    if (playerY < GAME_H / 2.0f) playerY = GAME_H / 2.0f;
    if (playerY > GAME_H - PLAYER_H) playerY = GAME_H - PLAYER_H;

    fireCooldown -= dt;
    if (keys[VK_SPACE] && fireCooldown <= 0.0f) {
        FireBullet();
        fireCooldown = 0.12f;
    }

    waveTime += dt;
    if (waveTime > 15.0f) { waveTime = 0.0f; wave++; }

    float spawnInterval = 0.9f - (float)(wave - 1) * 0.08f;
    if (spawnInterval < 0.25f) spawnInterval = 0.25f;
    spawnTimer -= dt;
    if (spawnTimer <= 0.0f) { SpawnEnemy(); spawnTimer = spawnInterval; }

    float enemySpeed = 60.0f + (float)(wave - 1) * 10.0f;

    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;
        bullets[i].y -= 480.0f * dt;
        if (bullets[i].y < -10.0f) bullets[i].active = FALSE;
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        enemies[i].y += enemySpeed * dt;
        enemies[i].x += enemies[i].vx * dt;
        if (enemies[i].x < 8.0f || enemies[i].x > GAME_W - 8.0f) enemies[i].vx *= -1.0f;
        if (enemies[i].y > GAME_H + 16.0f) { enemies[i].active = FALSE; continue; }

        float dx = enemies[i].x - playerX;
        float dy = enemies[i].y - playerY;
        if (dx < 14.0f && dx > -14.0f && dy < 14.0f && dy > -14.0f) {
            gameOver = TRUE;
        }

        for (int b = 0; b < MAX_BULLETS; b++) {
            if (!bullets[b].active) continue;
            float bx = bullets[b].x - enemies[i].x;
            float by = bullets[b].y - enemies[i].y;
            if (bx < 10.0f && bx > -10.0f && by < 10.0f && by > -10.0f) {
                bullets[b].active = FALSE;
                enemies[i].active = FALSE;
                score += 10;
                break;
            }
        }
    }

    titleTimer += dt;
    if (titleTimer > 0.2f) { titleTimer = 0.0f; UpdateTitle(); }
}

static void RenderGame(void) {
    ClearScreen(0xFF0A0A14);

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        FillPixelRect((int)enemies[i].x - 8, (int)enemies[i].y - 6, 16, 12, 0xFFE0563F);
    }

    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;
        FillPixelRect((int)bullets[i].x - 1, (int)bullets[i].y - 4, 2, 8, 0xFFF7E36B);
    }

    if (!gameOver) {
        FillPixelRect((int)playerX - 8, (int)playerY - 4, 16, 10, 0xFF4AD9E0);
        FillPixelRect((int)playerX - 2, (int)playerY - 10, 4, 8, 0xFF4AD9E0);
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
    bmi.bmiHeader.biHeight = -GAME_H; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(hwnd);

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
        if (dt > 0.05f) dt = 0.05f; /* 스파이크 방지 */

        UpdateGame(dt);
        RenderGame();

        RECT client;
        GetClientRect(hwnd, &client);
        StretchDIBits(hdc,
            0, 0, client.right, client.bottom,
            0, 0, GAME_W, GAME_H,
            framebuffer, &bmi, DIB_RGB_COLORS, SRCCOPY);

        Sleep(1); /* CPU 100% 점유 방지 */
    }

    ReleaseDC(hwnd, hdc);
    return 0;
}
