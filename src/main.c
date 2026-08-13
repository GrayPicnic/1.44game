/*
 * 1.44MB 게임 콘테스트용 "수동 사격 절차" 프로토타입.
 * - K9 자주포 비상 수동 사격 절차를 모티브로, 전장은 보여주지 않고
 *   포반원 시점에서 하달받은 제원대로 빠르고 정확하게 방열/장전/격발하는 게임.
 * - 외부 런타임 없음, 순수 Win32 + GDI. 텍스트는 7세그먼트 숫자를 직접 픽셀로 그려서 표시.
 * - 방위각은 실제 밀(mil) 대신 이해하기 쉬운 각도(0~359°)로 단순화했다.
 */

#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define GAME_W 640
#define GAME_H 360
#define WINDOW_SCALE 2
#define WINDOW_TITLE "1.44MB Fire Mission"

#define PI_F 3.14159265f
#define TIME_LIMIT 20.0f

#define AZ_TOL 3.0f
#define EL_TOL 2.0f
#define AZ_RATE 70.0f
#define EL_RATE 40.0f
#define EL_MIN 10.0f
#define EL_MAX 65.0f

#define COL_BG      0xFF161B12
#define COL_PANEL   0xFF20281A
#define COL_GREEN   0xFF3CFF6E
#define COL_AMBER   0xFFFFB020
#define COL_RED     0xFFFF4433
#define COL_DIM     0xFF3A4430

typedef enum {
    STAGE_AZIMUTH, STAGE_ELEVATION, STAGE_CHARGE,
    STAGE_LOADING, STAGE_BREECH, STAGE_FIRE, STAGE_RESULT
} Stage;

static uint32_t framebuffer[GAME_W * GAME_H];
static BITMAPINFO bmi;
static BOOL running = TRUE;
static BOOL keys[256], keysPrev[256];
static HWND g_hwnd;

static Stage stage;
static float elapsed;
static BOOL timeUp;

static float azTarget, azCurrent;
static float elTarget, elCurrent;
static int chTarget, chResult;
static float loadZoneStart;
static float loadStageTimer;
static BOOL loadHit;
static BOOL breechDone;

static BOOL azOk, elOk, chOk, loadOk;
static BOOL missionHit;

/* ---------- 저수준 렌더링 ---------- */

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
static void DrawRectOutline(int x0, int y0, int w, int h, uint32_t color) {
    FillPixelRect(x0, y0, w, 1, color);
    FillPixelRect(x0, y0 + h - 1, w, 1, color);
    FillPixelRect(x0, y0, 1, h, color);
    FillPixelRect(x0 + w - 1, y0, 1, h, color);
}
static void DrawRing(int cx, int cy, float radius, uint32_t color) {
    for (float a = 0.0f; a < 360.0f; a += 1.5f) {
        float rad = a * PI_F / 180.0f;
        PutPixel(cx + (int)(cosf(rad) * radius), cy + (int)(sinf(rad) * radius), color);
    }
}
static void DrawRadial(int cx, int cy, float valueDeg, float rFrom, float rTo, uint32_t color) {
    float rad = (valueDeg - 90.0f) * PI_F / 180.0f;
    float cs = cosf(rad), sn = sinf(rad);
    for (float rr = rFrom; rr <= rTo; rr += 1.0f) {
        int px = cx + (int)(cs * rr), py = cy + (int)(sn * rr);
        PutPixel(px, py, color);
        PutPixel(px + 1, py, color);
    }
}

/* ---------- 7세그먼트 숫자 ---------- */

static const int SEG[10] = { 63, 6, 91, 79, 102, 109, 125, 7, 127, 111 };

static void DrawDigit(int x, int y, int d, uint32_t color) {
    if (d < 0 || d > 9) return;
    int s = SEG[d];
    if (s & 1)  FillPixelRect(x + 2, y,      6, 2, color); /* a */
    if (s & 2)  FillPixelRect(x + 8, y + 1,  2, 7, color); /* b */
    if (s & 4)  FillPixelRect(x + 8, y + 9,  2, 7, color); /* c */
    if (s & 8)  FillPixelRect(x + 2, y + 16, 6, 2, color); /* d */
    if (s & 16) FillPixelRect(x,     y + 9,  2, 7, color); /* e */
    if (s & 32) FillPixelRect(x,     y + 1,  2, 7, color); /* f */
    if (s & 64) FillPixelRect(x + 2, y + 8,  6, 2, color); /* g */
}
static void DrawNumber(int x, int y, int value, int width, uint32_t color) {
    if (value < 0) value = 0;
    for (int i = width - 1; i >= 0; i--) {
        DrawDigit(x + i * 12, y, value % 10, color);
        value /= 10;
    }
}

/* ---------- 라운드 로직 ---------- */

static float RandRange(float lo, float hi) {
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}
static float AzDiff(float a, float b) {
    float d = fabsf(a - b);
    if (d > 180.0f) d = 360.0f - d;
    return d;
}

static void NewMission(void) {
    azTarget = (float)(rand() % 360);
    azCurrent = fmodf(azTarget + 140.0f + RandRange(0.0f, 80.0f), 360.0f);
    elTarget = RandRange(EL_MIN, EL_MAX);
    elCurrent = EL_MIN;
    chTarget = 1 + rand() % 7;
    chResult = 0;
    loadZoneStart = RandRange(20.0f, 150.0f);
    loadStageTimer = 0.0f;
    loadHit = FALSE;
    breechDone = FALSE;
    elapsed = 0.0f;
    timeUp = FALSE;
    stage = STAGE_AZIMUTH;
}

static void EvaluateMission(void) {
    azOk = AzDiff(azCurrent, azTarget) <= AZ_TOL;
    elOk = fabsf(elCurrent - elTarget) <= EL_TOL;
    chOk = chResult == chTarget;
    loadOk = loadHit;
    missionHit = azOk && elOk && chOk && loadOk && !timeUp;

    char buf[256];
    if (missionHit) {
        wsprintfA(buf, "%s  |  \xEB\xAA\x85\xEC\xA4\x91! R\xEB\xA1\x9C \xEB\x8B\xA4\xEC\x9D\x8C 사격",
            WINDOW_TITLE);
    } else if (timeUp) {
        wsprintfA(buf, "%s  |  \xEC\x8B\x9C\xEA\xB0\x84\xEC\xB4\x88\xEA\xB3\xBC - \xEB\xB9\x97\xEB\x82\x98\xEA\xB0\x90  |  R\xEB\xA1\x9C \xEC\x9E\xAC\xEC\x8B\x9C\xEB\x8F\x84",
            WINDOW_TITLE);
    } else {
        char reasons[200] = "";
        if (!azOk) lstrcatA(reasons, "\xEB\xB0\xA9\xEC\x9C\x84\xEA\xB0\x81\xEC\x98\xA4\xEC\xB0\xA8 ");
        if (!elOk) lstrcatA(reasons, "\xEC\x82\xAC\xEA\xB0\x81\xEC\x98\xA4\xEC\xB0\xA8 ");
        if (!chOk) lstrcatA(reasons, "\xEC\x9E\xA5\xEC\x95\xBD\xEB\xB2\x88\xED\x98\xB8\xED\x8B\x80\xEB\xA6\xBC ");
        if (!loadOk) lstrcatA(reasons, "\xEC\x9E\xA5\xEC\xA0\x84\xED\x83\x80\xEC\x9D\xB4\xEB\xB0\x8D\xEB\xB6\x88\xEB\x9F\x89 ");
        wsprintfA(buf, "%s  |  \xEB\xB9\x97\xEB\x82\x98\xEA\xB0\x90 - \xEC\x9B\x90\xEC\x9D\xB8: %s |  R\xEB\xA1\x9C \xEC\x9E\xAC\xEC\x8B\x9C\xEB\x8F\x84",
            WINDOW_TITLE, reasons);
    }
    SetWindowTextA(g_hwnd, buf);
}

static void UpdateGame(float dt) {
    if (stage == STAGE_RESULT) {
        if (keys['R'] && !keysPrev['R']) NewMission();
        return;
    }

    elapsed += dt;
    if (elapsed >= TIME_LIMIT && !timeUp) {
        timeUp = TRUE;
        stage = STAGE_RESULT;
        EvaluateMission();
        return;
    }

    switch (stage) {
        case STAGE_AZIMUTH:
            if (keys[VK_LEFT] || keys['A']) azCurrent -= AZ_RATE * dt;
            if (keys[VK_RIGHT] || keys['D']) azCurrent += AZ_RATE * dt;
            azCurrent = fmodf(azCurrent + 360.0f, 360.0f);
            if (keys[VK_SPACE] && !keysPrev[VK_SPACE]) stage = STAGE_ELEVATION;
            break;
        case STAGE_ELEVATION:
            if (keys[VK_UP] || keys['W']) elCurrent += EL_RATE * dt;
            if (keys[VK_DOWN] || keys['S']) elCurrent -= EL_RATE * dt;
            if (elCurrent < 0.0f) elCurrent = 0.0f;
            if (elCurrent > 75.0f) elCurrent = 75.0f;
            if (keys[VK_SPACE] && !keysPrev[VK_SPACE]) stage = STAGE_CHARGE;
            break;
        case STAGE_CHARGE:
            for (int n = 1; n <= 7; n++) {
                if (keys['0' + n] && !keysPrev['0' + n]) {
                    chResult = n;
                    stage = STAGE_LOADING;
                }
            }
            break;
        case STAGE_LOADING: {
            loadStageTimer += dt;
            if (keys[VK_SPACE] && !keysPrev[VK_SPACE]) {
                float t = fmodf(loadStageTimer, 1.2f);
                float pos = (t < 0.6f) ? (t / 0.6f * 200.0f) : ((1.2f - t) / 0.6f * 200.0f);
                loadHit = (pos >= loadZoneStart && pos <= loadZoneStart + 30.0f);
                stage = STAGE_BREECH;
            }
            break;
        }
        case STAGE_BREECH:
            if (keys[VK_SPACE] && !keysPrev[VK_SPACE]) {
                breechDone = TRUE;
                stage = STAGE_FIRE;
            }
            break;
        case STAGE_FIRE:
            if (keys[VK_RETURN] && !keysPrev[VK_RETURN]) {
                stage = STAGE_RESULT;
                EvaluateMission();
            }
            break;
        default: break;
    }

    for (int i = 0; i < 256; i++) keysPrev[i] = keys[i];
}

/* ---------- 렌더링 ---------- */

static void RenderGame(void) {
    ClearScreen(COL_BG);
    DrawRectOutline(10, 10, GAME_W - 20, GAME_H - 20, COL_DIM);

    /* 남은 시간 */
    int timeLeft = (int)(TIME_LIMIT - elapsed + 0.99f);
    if (timeLeft < 0) timeLeft = 0;
    DrawNumber(GAME_W / 2 - 12, 20, timeLeft, 2, timeLeft <= 5 ? COL_RED : COL_GREEN);

    /* 방위각 다이얼 */
    int azCx = 130, azCy = 130;
    DrawRing(azCx, azCy, 55.0f, COL_DIM);
    DrawRadial(azCx, azCy, azTarget, 55.0f, 66.0f, COL_AMBER);
    DrawRadial(azCx, azCy, azCurrent, 0.0f, 46.0f,
        stage == STAGE_AZIMUTH ? COL_GREEN : COL_DIM);
    DrawNumber(azCx - 30, azCy + 75, (int)azTarget, 3, COL_AMBER);
    DrawNumber(azCx + 6, azCy + 75, (int)azCurrent, 3, COL_GREEN);

    /* 사각 바 게이지 */
    int elX = 260, elY = 60, elW = 26, elH = 150;
    DrawRectOutline(elX, elY, elW, elH, COL_DIM);
    int fillH = (int)(elCurrent / 75.0f * (float)elH);
    FillPixelRect(elX + 2, elY + elH - fillH, elW - 4, fillH,
        stage == STAGE_ELEVATION ? COL_GREEN : COL_DIM);
    int tickY = elY + elH - (int)(elTarget / 75.0f * (float)elH);
    FillPixelRect(elX - 6, tickY, elW + 12, 2, COL_AMBER);
    DrawNumber(elX - 4, elY + elH + 14, (int)elTarget, 2, COL_AMBER);
    DrawNumber(elX + 22, elY + elH + 14, (int)elCurrent, 2, COL_GREEN);

    /* 장약 번호 */
    int chX = 380, chY = 60;
    for (int i = 1; i <= 7; i++) {
        uint32_t c = (i == chTarget) ? COL_AMBER : COL_DIM;
        if (stage > STAGE_CHARGE && i == chResult) c = chResult == chTarget ? COL_GREEN : COL_RED;
        DrawRectOutline(chX + (i - 1) * 26, chY, 20, 24, c);
        DrawNumber(chX + (i - 1) * 26 + 5, chY + 3, i, 1, c);
    }

    /* 장전 타이밍 바 */
    int ldX = 380, ldY = 140, ldW = 200, ldH = 16;
    DrawRectOutline(ldX, ldY, ldW, ldH, COL_DIM);
    FillPixelRect(ldX + (int)loadZoneStart, ldY, 30, ldH, 0xFF2A3A22);
    if (stage == STAGE_LOADING) {
        float t = fmodf(loadStageTimer, 1.2f);
        float pos = (t < 0.6f) ? (t / 0.6f * (float)ldW) : ((1.2f - t) / 0.6f * (float)ldW);
        FillPixelRect(ldX + (int)pos - 2, ldY - 4, 4, ldH + 8, COL_GREEN);
    } else if (stage > STAGE_LOADING) {
        FillPixelRect(ldX + 4, ldY + 4, ldW - 8, ldH - 8, loadHit ? COL_GREEN : COL_RED);
    }

    /* 폐쇄/뇌관 + 격발 */
    DrawRectOutline(380, 200, 200, 30, breechDone ? COL_GREEN : COL_DIM);
    DrawRectOutline(380, 240, 200, 30, stage == STAGE_FIRE ? COL_AMBER : COL_DIM);

    /* 결과 배너 */
    if (stage == STAGE_RESULT) {
        uint32_t rc = missionHit ? COL_GREEN : COL_RED;
        FillPixelRect(GAME_W / 2 - 70, GAME_H - 60, 140, 30, 0xFF000000);
        DrawRectOutline(GAME_W / 2 - 70, GAME_H - 60, 140, 30, rc);
    }
}

/* ---------- Win32 뼈대 ---------- */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DESTROY: running = FALSE; PostQuitMessage(0); return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) running = FALSE;
            if (wp < 256) keys[wp] = TRUE;
            return 0;
        case WM_KEYUP:
            if (wp < 256) keys[wp] = FALSE;
            return 0;
        default: return DefWindowProc(hwnd, msg, wp, lp);
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
    HWND hwnd = CreateWindowA("GameWindowClass", WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
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
    NewMission();

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
        StretchDIBits(hdc, 0, 0, client.right, client.bottom,
            0, 0, GAME_W, GAME_H, framebuffer, &bmi, DIB_RGB_COLORS, SRCCOPY);
        Sleep(1);
    }
    ReleaseDC(hwnd, hdc);
    return 0;
}
