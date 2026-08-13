/*
 * 1.44MB 게임 콘테스트용 화면 골격(스캐폴드) 프로토타입.
 * 로비 -> 인트로 -> 조작화면 (+ 옵션 오버레이) 전체 흐름.
 * 실제 아트는 전부 나중에 이미지로 교체할 자리 -- 지금은 색깔로만 구분한 사각형.
 * 텍스트는 로마자만 있으면 되는 영어 라벨이라 5x7 도트매트릭스 폰트를 직접 그렸다.
 * (한/일 다국어는 문구를 이미지로 미리 그리는 별도 파이프라인이 필요 -- 다음 단계)
 */

#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define GAME_W 640
#define GAME_H 360
#define WINDOW_SCALE 2
#define WINDOW_TITLE "1.44MB Game - Scaffold"
#define PI_F 3.14159265f
#define GAMEPLAY_TIME_LIMIT 30.0f

typedef enum { SCENE_LOBBY, SCENE_OPTIONS, SCENE_INTRO, SCENE_GAMEPLAY } Scene;

static uint32_t framebuffer[GAME_W * GAME_H];
static BITMAPINFO bmi;
static BOOL running = TRUE;
static BOOL keys[256], keysPrev[256];
static HWND g_hwnd;

static int mouseX, mouseY;
static BOOL mouseDown, mouseDownPrev;

static Scene scene = SCENE_LOBBY;
static Scene sceneBeforeOptions = SCENE_LOBBY;

static BOOL soundOn = TRUE;
static int langIndex = 0; /* 0=EN 1=KR 2=JP -- placeholder cycle, only EN text exists for now */
static const char *LANG_NAMES[3] = { "EN", "KR", "JP" };

static float introTimer = 0.0f;

static float gpElapsed = 0.0f;
static float bubbleTimer = 0.0f;
static int bubbleDigit = 0;
static float leverAngle = 90.0f;
static BOOL draggingLever = FALSE;
static int leverCx = 560, leverCy = 190;
static const int leverPivotGrabR = 70;

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

/* ---------- 7세그먼트 숫자 (게이지/큰 숫자용) ---------- */

static const int SEG[10] = { 63, 6, 91, 79, 102, 109, 125, 7, 127, 111 };
static void DrawDigit(int x, int y, int d, uint32_t color) {
    if (d < 0 || d > 9) return;
    int s = SEG[d];
    if (s & 1)  FillPixelRect(x + 2, y,      6, 2, color);
    if (s & 2)  FillPixelRect(x + 8, y + 1,  2, 7, color);
    if (s & 4)  FillPixelRect(x + 8, y + 9,  2, 7, color);
    if (s & 8)  FillPixelRect(x + 2, y + 16, 6, 2, color);
    if (s & 16) FillPixelRect(x,     y + 9,  2, 7, color);
    if (s & 32) FillPixelRect(x,     y + 1,  2, 7, color);
    if (s & 64) FillPixelRect(x + 2, y + 8,  6, 2, color);
}
static void DrawNumber(int x, int y, int value, int width, uint32_t color) {
    if (value < 0) value = 0;
    for (int i = width - 1; i >= 0; i--) {
        DrawDigit(x + i * 12, y, value % 10, color);
        value /= 10;
    }
}

/* ---------- 5x7 도트매트릭스 폰트 (영문 라벨용) ---------- */

typedef struct { char c; unsigned char rows[7]; } Glyph;
static const Glyph FONT5X7[] = {
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}},
    {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F}},
    {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
    {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
    {'N', {0x11,0x19,0x15,0x15,0x13,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
    {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
    {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
    {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {':', {0x00,0x04,0x00,0x00,0x00,0x04,0x00}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
    {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {'!', {0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
    {'?', {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}},
};
#define FONT_COUNT (int)(sizeof(FONT5X7) / sizeof(FONT5X7[0]))

static void DrawChar(int x, int y, char c, uint32_t color, int scale) {
    if (c == ' ') return;
    for (int i = 0; i < FONT_COUNT; i++) {
        if (FONT5X7[i].c != c) continue;
        for (int row = 0; row < 7; row++) {
            unsigned char bits = FONT5X7[i].rows[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (1 << (4 - col))) {
                    FillPixelRect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        return;
    }
}
static void DrawLabel(int x, int y, const char *text, uint32_t color, int scale) {
    int cx = x;
    for (const char *p = text; *p; p++) {
        DrawChar(cx, y, *p, color, scale);
        cx += 6 * scale;
    }
}
static int TextWidth(const char *text, int scale) {
    int len = 0;
    for (const char *p = text; *p; p++) len++;
    return len * 6 * scale;
}

/* ---------- 버튼 ---------- */

typedef struct { int x, y, w, h; uint32_t color; const char *label; } Button;

static BOOL PointInRect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}
static BOOL ButtonClicked(Button *b) {
    BOOL hover = PointInRect(mouseX, mouseY, b->x, b->y, b->w, b->h);
    uint32_t c = b->color;
    if (hover) {
        int r = (int)((c >> 16) & 0xFF) + 30; if (r > 255) r = 255;
        int g = (int)((c >> 8) & 0xFF) + 30;  if (g > 255) g = 255;
        int bl = (int)(c & 0xFF) + 30;        if (bl > 255) bl = 255;
        c = 0xFF000000 | (r << 16) | (g << 8) | bl;
    }
    FillPixelRect(b->x, b->y, b->w, b->h, c);
    DrawRectOutline(b->x, b->y, b->w, b->h, 0xFFEEEEEE);
    int tw = TextWidth(b->label, 2);
    DrawLabel(b->x + (b->w - tw) / 2, b->y + b->h / 2 - 7, b->label, 0xFFFFFFFF, 2);
    return hover && mouseDown && !mouseDownPrev;
}

/* ---------- 씬: 로비 ---------- */

static void RenderLobby(void) {
    ClearScreen(0xFF1B3A3A); /* bg placeholder */

    FillPixelRect(170, 40, 300, 90, 0xFF6B3FA0); /* title image placeholder */
    DrawRectOutline(170, 40, 300, 90, 0xFFEEEEEE);
    DrawLabel(170 + 60, 40 + 40, "GAME TITLE", 0xFFFFFFFF, 2);

    Button startBtn = { 220, 170, 200, 44, 0xFF2E8B57, "START" };
    Button optBtn   = { 220, 226, 200, 44, 0xFF3060B0, "OPTIONS" };
    Button exitBtn  = { 220, 282, 200, 44, 0xFFA03030, "EXIT" };

    if (ButtonClicked(&startBtn)) { scene = SCENE_INTRO; introTimer = 0.0f; }
    if (ButtonClicked(&optBtn))   { sceneBeforeOptions = SCENE_LOBBY; scene = SCENE_OPTIONS; }
    if (ButtonClicked(&exitBtn))  { running = FALSE; }
}

/* ---------- 씬: 옵션 오버레이 ---------- */

static void RenderOptions(void) {
    FillPixelRect(140, 90, 360, 180, 0xFF2A2A3A);
    DrawRectOutline(140, 90, 360, 180, 0xFFEEEEEE);
    DrawLabel(160, 110, "OPTIONS", 0xFFFFFFFF, 2);

    char soundLabel[24];
    wsprintfA(soundLabel, "SOUND: %s", soundOn ? "ON" : "OFF");
    Button soundBtn = { 170, 150, 300, 40, 0xFFD08020, soundLabel };
    if (ButtonClicked(&soundBtn)) soundOn = !soundOn;

    char langLabel[24];
    wsprintfA(langLabel, "LANGUAGE: %s", LANG_NAMES[langIndex]);
    Button langBtn = { 170, 200, 300, 40, 0xFF20A0A0, langLabel };
    if (ButtonClicked(&langBtn)) langIndex = (langIndex + 1) % 3;

    Button backBtn = { 170, 250, 300, 40, 0xFF606060, "BACK" };
    if (ButtonClicked(&backBtn) || (keys[VK_ESCAPE] && !keysPrev[VK_ESCAPE])) {
        scene = sceneBeforeOptions;
    }
}

/* ---------- 씬: 인트로 ---------- */

static void RenderIntro(void) {
    ClearScreen(0xFF241B45); /* bg placeholder */

    FillPixelRect(40, 260, GAME_W - 80, 70, 0xFF4A3524); /* npc dialogue box */
    DrawRectOutline(40, 260, GAME_W - 80, 70, 0xFFEEEEEE);
    DrawLabel(60, 280, "FIRE MISSION INCOMING. STAND BY.", 0xFFFFE0B0, 2);

    DrawLabel(GAME_W - 150, GAME_H - 20, "SKIP: ESC", 0xFFAAAAAA, 1);

    introTimer += 1.0f / 60.0f; /* 대략적인 진행 표시용, 정밀 dt는 UpdateGame에서 처리 */
}

static void UpdateIntro(float dt) {
    (void)dt;
    if (keys[VK_ESCAPE] && !keysPrev[VK_ESCAPE]) {
        scene = SCENE_GAMEPLAY; gpElapsed = 0.0f; bubbleTimer = 0.0f;
    }
    if ((keys[VK_SPACE] && !keysPrev[VK_SPACE]) || (mouseDown && !mouseDownPrev)) {
        scene = SCENE_GAMEPLAY; gpElapsed = 0.0f; bubbleTimer = 0.0f;
    }
}

/* ---------- 씬: 조작화면 ---------- */

static void UpdateGameplay(float dt) {
    if (keys[VK_ESCAPE] && !keysPrev[VK_ESCAPE]) { scene = SCENE_LOBBY; return; }

    gpElapsed += dt;
    bubbleTimer += dt;
    if (bubbleTimer >= 1.0f) { bubbleTimer = 0.0f; bubbleDigit = rand() % 10; }

    /* 레버 드래그 */
    int dx = mouseX - leverCx, dy = mouseY - leverCy;
    float distToPivot = sqrtf((float)(dx * dx + dy * dy));
    if (mouseDown && !mouseDownPrev && distToPivot <= leverPivotGrabR) {
        draggingLever = TRUE;
        SetCapture(g_hwnd);
    }
    if (!mouseDown) {
        if (draggingLever) ReleaseCapture();
        draggingLever = FALSE;
    }
    if (draggingLever) {
        float rad = atan2f((float)dy, (float)dx);
        float deg = rad * 180.0f / PI_F; /* -180..180, 0=오른쪽 */
        leverAngle = deg;
    }
}

static void RenderGameplay(void) {
    ClearScreen(0xFF161B12); /* bg placeholder */

    /* 좌측 숫자 말풍선 */
    FillPixelRect(30, 130, 110, 100, 0xFF6A2540);
    DrawRectOutline(30, 130, 110, 100, 0xFFEEEEEE);
    DrawNumber(30 + 40, 130 + 40, bubbleDigit, 1, 0xFFFFFFFF);

    /* 상단 타이머 게이지 */
    float timeLeft = GAMEPLAY_TIME_LIMIT - gpElapsed;
    if (timeLeft < 0.0f) timeLeft = 0.0f;
    int barW = 300, barH = 14, barX = GAME_W / 2 - barW / 2, barY = 16;
    DrawRectOutline(barX, barY, barW, barH, 0xFFAAAAAA);
    int fillW = (int)(timeLeft / GAMEPLAY_TIME_LIMIT * (float)(barW - 4));
    if (fillW < 0) fillW = 0;
    BOOL lowTime = timeLeft <= 5.0f;
    BOOL blinkOn = fmodf(gpElapsed, 0.4f) < 0.2f;
    uint32_t gaugeColor = lowTime ? (blinkOn ? 0xFFFF4433 : 0xFF662222) : 0xFF3CFF6E;
    FillPixelRect(barX + 2, barY + 2, fillW, barH - 4, gaugeColor);
    DrawNumber(barX + barW + 12, barY - 2, (int)(timeLeft + 0.99f), 2, lowTime ? 0xFFFF4433 : 0xFF3CFF6E);

    /* 중앙 숫자판(계량기 자리, 지금은 레버 각도값 표시) */
    FillPixelRect(230, 140, 160, 100, 0xFF1E2A4A);
    DrawRectOutline(230, 140, 160, 100, 0xFFEEEEEE);
    DrawLabel(250, 150, "METER", 0xFF88AACC, 1);
    int meterValue = (int)((leverAngle + 180.0f)); /* 0..360 표시용 */
    DrawNumber(255, 190, meterValue, 3, 0xFF3CFF6E);

    /* 우측 레버 */
    DrawRing(leverCx, leverCy, 8.0f, 0xFF8A8A9A);
    float rad = leverAngle * PI_F / 180.0f;
    int hx = leverCx + (int)(cosf(rad) * 60.0f);
    int hy = leverCy + (int)(sinf(rad) * 60.0f);
    for (float t = 0.0f; t <= 1.0f; t += 0.02f) {
        int px = leverCx + (int)((hx - leverCx) * t);
        int py = leverCy + (int)((hy - leverCy) * t);
        FillPixelRect(px - 1, py - 1, 3, 3, draggingLever ? 0xFFFFB020 : 0xFF8A8A9A);
    }
    DrawRing(leverCx, leverCy, (float)leverPivotGrabR, 0xFF333333);

    /* 하단 단축키 안내 */
    DrawLabel(20, GAME_H - 20, "DRAG LEVER: ADJUST", 0xFFAAAAAA, 1);
    DrawLabel(200, GAME_H - 20, "SPACE: CONFIRM", 0xFFAAAAAA, 1);
    int escW = TextWidth("ESC: QUIT", 1);
    DrawLabel(GAME_W - 20 - escW, GAME_H - 20, "ESC: QUIT", 0xFFAAAAAA, 1);
}

/* ---------- 메인 업데이트/렌더 ---------- */

static void UpdateGame(float dt) {
    switch (scene) {
        case SCENE_INTRO: UpdateIntro(dt); break;
        case SCENE_GAMEPLAY: UpdateGameplay(dt); break;
        default: break;
    }
    for (int i = 0; i < 256; i++) keysPrev[i] = keys[i];
    mouseDownPrev = mouseDown;
}

static void RenderGame(void) {
    switch (scene) {
        case SCENE_LOBBY: RenderLobby(); break;
        case SCENE_OPTIONS: RenderLobby(); RenderOptions(); break; /* 이전 화면 위에 오버레이 */
        case SCENE_INTRO: RenderIntro(); break;
        case SCENE_GAMEPLAY: RenderGameplay(); break;
    }
}

/* ---------- Win32 뼈대 ---------- */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DESTROY: running = FALSE; PostQuitMessage(0); return 0;
        case WM_KEYDOWN:
            if (wp < 256) keys[wp] = TRUE;
            return 0;
        case WM_KEYUP:
            if (wp < 256) keys[wp] = FALSE;
            return 0;
        case WM_MOUSEMOVE:
            /* 창은 WINDOW_SCALE배로 확대되어 있으니 게임 좌표계(640x360)로 되돌린다 */
            mouseX = (short)LOWORD(lp) / WINDOW_SCALE;
            mouseY = (short)HIWORD(lp) / WINDOW_SCALE;
            return 0;
        case WM_LBUTTONDOWN:
            mouseDown = TRUE;
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP:
            mouseDown = FALSE;
            ReleaseCapture();
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
