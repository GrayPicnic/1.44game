/*
 * 1.44MB 게임 콘테스트용 최소 골격.
 * - 외부 런타임(SDL, DirectX 등) 없이 순수 Win32 API + GDI 소프트웨어 렌더링만 사용.
 * - 정적 링크 + -Os + strip 으로 실행파일을 최대한 작게 유지.
 * - 고정 해상도 픽셀 버퍼를 만들어 창 크기에 맞춰 확대 출력한다 (레트로 픽셀아트에 적합).
 */

#include <windows.h>
#include <stdint.h>

#define GAME_W 320
#define GAME_H 180
#define WINDOW_SCALE 3
#define WINDOW_TITLE "1.44MB Game"

static uint32_t framebuffer[GAME_W * GAME_H];
static BITMAPINFO bmi;
static BOOL running = TRUE;
static BOOL keys[256];

/* 플레이어 예시 상태 - 실제 게임 로직으로 교체할 자리 */
static float playerX = GAME_W / 2.0f;
static float playerY = GAME_H / 2.0f;

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

static void UpdateGame(float dt) {
    float speed = 90.0f; /* px/sec */
    if (keys[VK_LEFT]  || keys['A']) playerX -= speed * dt;
    if (keys[VK_RIGHT] || keys['D']) playerX += speed * dt;
    if (keys[VK_UP]    || keys['W']) playerY -= speed * dt;
    if (keys[VK_DOWN]  || keys['S']) playerY += speed * dt;

    if (playerX < 0) playerX = 0;
    if (playerY < 0) playerY = 0;
    if (playerX > GAME_W - 8) playerX = GAME_W - 8;
    if (playerY > GAME_H - 8) playerY = GAME_H - 8;
}

static void RenderGame(void) {
    ClearScreen(0xFF202030);
    FillPixelRect((int)playerX, (int)playerY, 8, 8, 0xFFFFD84A);
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

        Sleep(1); /* CPU 100% 점유 방지 (간단한 방식, 필요하면 정밀 타이머로 교체) */
    }

    ReleaseDC(hwnd, hdc);
    return 0;
}
