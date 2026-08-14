/*
 * 1.44MB 게임 콘테스트용 화면 골격(스캐폴드) 프로토타입.
 * 로비 -> 인트로 -> 조작화면 (+ 옵션 오버레이) 전체 흐름.
 * 실제 아트는 전부 나중에 이미지로 교체할 자리 -- 지금은 색깔로만 구분한 사각형.
 * 텍스트는 로마자만 있으면 되는 영어 라벨이라 5x7 도트매트릭스 폰트를 직접 그렸다.
 * (한/일 다국어는 문구를 이미지로 미리 그리는 별도 파이프라인이 필요 -- 다음 단계)
 */

#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define GAME_W 640
#define GAME_H 360
#define WINDOW_SCALE 2
#define WINDOW_TITLE "1.44MB Game - Scaffold"
#define PI_F 3.14159265f
#define GAMEPLAY_TIME_LIMIT 60.0f
#define MISSION_FAILED_HOLD_TIME 3.0f

typedef enum { SCENE_LOBBY, SCENE_OPTIONS, SCENE_INTRO, SCENE_GAMEPLAY } Scene;

static uint32_t framebuffer[GAME_W * GAME_H];
static BITMAPINFO bmi;
static BOOL running = TRUE;
static BOOL keys[256], keysPrev[256];
static HWND g_hwnd;

static int mouseX, mouseY;
static BOOL mouseDown, mouseDownPrev;
static BOOL rMouseDown, rMouseDownPrev;

static Scene scene = SCENE_LOBBY;
static Scene sceneBeforeOptions = SCENE_LOBBY;

static BOOL soundOn = TRUE;
static int langIndex = 0; /* 0=EN 1=KR 2=JP -- placeholder cycle, only EN text exists for now */
static const char *LANG_NAMES[3] = { "EN", "KR", "JP" };

static float introTimer = 0.0f;

/* ---------- 포병 밀(Mil) 단위 사격제원 ---------- */
/* 원 한 바퀴 = 6400밀. 방위각은 여러 바퀴 돌려서 맞추고, 사각은 -44~1244밀 범위의
   세로 릴로 맞춘다 (음수 구간은 이번 프로토타입에서 스킵 -- 실사용 범위 300~1100 위주). */
#define MIL_FULL 6400.0f
#define AZ_FINE_STEP_MIL 1.0f
#define AZ_GEAR_TURNS 20.0f  /* 핸들을 최대 20바퀴 돌려야 전체 범위(6400밀)를 커버 */
#define AZ_MIL_PER_DEG (MIL_FULL / AZ_GEAR_TURNS / 360.0f)

#define EL_TARGET_MIN 300
#define EL_TARGET_MAX 1100
#define EL_ADJUST_MIN 0.0f
#define EL_ADJUST_MAX 1244.0f
#define EL_FINE_STEP_MIL 1.0f
#define EL_DRAG_MIL_PER_PX 1.0f     /* 예전엔 4밀/px라 살짝만 끌어도 확 튀었음 -- 1:1로 완화 */
#define EL_TICK_STEP 1              /* 1밀 단위로 하나하나 보이게 */
#define EL_PX_PER_MIL 24.0f         /* 눈금이 1밀 간격이라 라벨 안 겹치게 픽셀 간격을 넉넉히 */
#define EL_FRICTION 4.0f            /* 클수록 관성이 빨리 멈춤 */
#define EL_VELOCITY_STOP_MIL 5.0f
#define EL_GRAB_HALF_W 30           /* 드래그 시작 판정 영역 (릴 폭에 맞춤) */
#define EL_GRAB_HALF_H 150          /* 위아래로 넉넉하게 -- 릴 높이(260)보다 살짝 더 */

/* 최종 판정: 방위각 오차 + 사각 오차(밀 단위 절댓값)의 합이 이 값 이하면 명중 */
#define TOTAL_ERROR_SUCCESS_MAX 5.0f

#define TRANS_TIME 0.4f
#define LEVER_FLASH_TIME 0.15f

#define BUBBLE_DIGIT_TIME 0.7f
#define BUBBLE_GAP_TIME 0.25f
#define BUBBLE_PAUSE_TIME 1.0f

/* 4자리 콜아웃: 표시-공백-표시-공백-표시-공백-표시-긴침묵 (8단계) */
static const float BUBBLE_PHASE_DUR[8] = {
    BUBBLE_DIGIT_TIME, BUBBLE_GAP_TIME, BUBBLE_DIGIT_TIME, BUBBLE_GAP_TIME,
    BUBBLE_DIGIT_TIME, BUBBLE_GAP_TIME, BUBBLE_DIGIT_TIME, BUBBLE_PAUSE_TIME
};

typedef enum { FIRESTAGE_AZIMUTH, FIRESTAGE_ELEVATION } FireStage;
typedef enum { TRANS_NONE, TRANS_OUT, TRANS_IN } TransState;
typedef enum { RESULT_NONE, RESULT_HIT, RESULT_MISS } FinalResult;

static float gpElapsed = 0.0f;
static FireStage fireStage = FIRESTAGE_AZIMUTH;
static TransState transState = TRANS_NONE;
static float transTimer = 0.0f;

static int azTargetMil = 0;
static float azAngleDeg = 0.0f;    /* 무제한 누적 회전각(도) -- 여러 바퀴 돌려도 안 끊김 */

static int elTargetMil = 0;
static float elCurrentMil = 0.0f;  /* 0..1244 */
static float elVelocity = 0.0f;    /* mil/sec -- 드래그 놓은 뒤 관성 감속용 */

static int bubblePhase = 0;        /* 0..7, 짝수=표시 홀수=공백. BUBBLE_PHASE_DUR 참고 */
static float bubbleTimer = 0.0f;

static float leverFlash = 0.0f;
static BOOL draggingLever = FALSE;
static float lastDragAngle = 0.0f;
static int lastDragMouseY = 0;

static BOOL missionFailed = FALSE;
static float failTimer = 0.0f;

static float azErrorRecorded = 0.0f;
static float elErrorRecorded = 0.0f;
static FinalResult finalResult = RESULT_NONE;
static float resultTimer = 0.0f;

static int leverCx = 560, leverCy = 190;
static const int leverPivotGrabR = 70;

static float NormalizeMil(float mil) {
    float m = fmodf(mil, MIL_FULL);
    if (m < 0.0f) m += MIL_FULL;
    return m;
}
static float MilDiff(float a, float b) {
    float d = fabsf(a - b);
    if (d > MIL_FULL / 2.0f) d = MIL_FULL - d;
    return d;
}

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
static void FillPixelRectAlpha(int x0, int y0, int w, int h, uint32_t color, float alpha) {
    if (alpha <= 0.0f) return;
    if (alpha > 1.0f) alpha = 1.0f;
    int cr = (int)((color >> 16) & 0xFF), cg = (int)((color >> 8) & 0xFF), cb = (int)(color & 0xFF);
    for (int y = y0; y < y0 + h; y++) {
        if (y < 0 || y >= GAME_H) continue;
        for (int x = x0; x < x0 + w; x++) {
            if (x < 0 || x >= GAME_W) continue;
            uint32_t old = framebuffer[y * GAME_W + x];
            int orr = (int)((old >> 16) & 0xFF), og = (int)((old >> 8) & 0xFF), ob = (int)(old & 0xFF);
            int r = (int)((float)orr * (1.0f - alpha) + (float)cr * alpha);
            int g = (int)((float)og * (1.0f - alpha) + (float)cg * alpha);
            int b = (int)((float)ob * (1.0f - alpha) + (float)cb * alpha);
            framebuffer[y * GAME_W + x] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}
static void DrawArrowRight(int tipX, int tipY, int size, uint32_t color) {
    for (int i = 0; i < size; i++) {
        int rowH = (size - i) * 2;
        FillPixelRect(tipX - size + i, tipY - rowH / 2, 1, rowH, color);
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

static void EnterGameplay(void) {
    scene = SCENE_GAMEPLAY;
    gpElapsed = 0.0f;
    fireStage = FIRESTAGE_AZIMUTH;
    transState = TRANS_NONE;
    transTimer = 0.0f;

    azTargetMil = rand() % (int)MIL_FULL;
    azAngleDeg = 0.0f;

    elTargetMil = EL_TARGET_MIN + rand() % (EL_TARGET_MAX - EL_TARGET_MIN + 1);
    elCurrentMil = 0.0f;
    elVelocity = 0.0f;

    bubblePhase = 0;
    bubbleTimer = 0.0f;

    missionFailed = FALSE;
    failTimer = 0.0f;

    azErrorRecorded = 0.0f;
    elErrorRecorded = 0.0f;
    finalResult = RESULT_NONE;
    resultTimer = 0.0f;

    draggingLever = FALSE;
}

static void UpdateIntro(float dt) {
    (void)dt;
    if (keys[VK_ESCAPE] && !keysPrev[VK_ESCAPE]) { EnterGameplay(); return; }
    if ((keys[VK_SPACE] && !keysPrev[VK_SPACE]) || (mouseDown && !mouseDownPrev)) { EnterGameplay(); return; }
}

/* ---------- 씬: 조작화면 ---------- */

static void UpdateGameplay(float dt) {
    if (missionFailed) {
        /* 조작 불가 -- 검정화면 홀드 후 로비로 */
        failTimer += dt;
        if (failTimer >= MISSION_FAILED_HOLD_TIME) scene = SCENE_LOBBY;
        return;
    }
    if (finalResult != RESULT_NONE && transState == TRANS_NONE) {
        /* 최종 결과 화면 홀드 후 로비로 */
        resultTimer += dt;
        if (resultTimer >= MISSION_FAILED_HOLD_TIME) scene = SCENE_LOBBY;
        return;
    }

    if (keys[VK_ESCAPE] && !keysPrev[VK_ESCAPE]) { scene = SCENE_LOBBY; return; }

    gpElapsed += dt;
    /* 이미 스페이스로 확정하고 전환 중이면 막판에 타임오버로 덮어쓰지 않는다 */
    if (gpElapsed >= GAMEPLAY_TIME_LIMIT && transState == TRANS_NONE && finalResult == RESULT_NONE) {
        missionFailed = TRUE;
        failTimer = 0.0f;
        return;
    }

    /* 말풍선: 현재 스테이지의 4자리 목표값을 자릿수 단위로 불러줌. 자릿수 사이/
       한바퀴 끝에는 배경(핑크 박스)까지 통째로 잠깐 사라짐 (bubblePhase 홀수 = 공백) */
    bubbleTimer += dt;
    if (bubbleTimer >= BUBBLE_PHASE_DUR[bubblePhase]) {
        bubbleTimer = 0.0f;
        bubblePhase = (bubblePhase + 1) % 8;
    }

    if (leverFlash > 0.0f) leverFlash -= dt;

    /* 스페이스로 확정 -> 페이드아웃 -> (다음 스테이지면 페이드인, 마지막이면 최종판정 후 페이드인) */
    if (transState == TRANS_OUT) {
        transTimer += dt;
        if (transTimer >= TRANS_TIME) {
            if (fireStage == FIRESTAGE_AZIMUTH) {
                fireStage = FIRESTAGE_ELEVATION;
                bubblePhase = 0;
                bubbleTimer = 0.0f;
                transState = TRANS_IN;
                transTimer = 0.0f;
            } else {
                float totalError = azErrorRecorded + elErrorRecorded;
                finalResult = (totalError <= TOTAL_ERROR_SUCCESS_MAX) ? RESULT_HIT : RESULT_MISS;
                transState = TRANS_IN;
                transTimer = 0.0f;
            }
        }
        return;
    }
    if (transState == TRANS_IN) {
        transTimer += dt;
        if (transTimer >= TRANS_TIME) transState = TRANS_NONE;
        return;
    }

    int dx = mouseX - leverCx, dy = mouseY - leverCy;
    float distToPivot = sqrtf((float)(dx * dx + dy * dy));

    if (fireStage == FIRESTAGE_AZIMUTH) {
        /* 좌클릭 드래그: 기어비 적용 -- 전체 범위(6400밀)를 커버하려면 최대 10바퀴 돌려야 함 */
        if (mouseDown && !mouseDownPrev && distToPivot <= leverPivotGrabR) {
            draggingLever = TRUE;
            lastDragAngle = atan2f((float)dy, (float)dx) * 180.0f / PI_F;
        }
        if (!mouseDown) draggingLever = FALSE;
        if (draggingLever) {
            float curAngle = atan2f((float)dy, (float)dx) * 180.0f / PI_F;
            float delta = curAngle - lastDragAngle;
            while (delta > 180.0f) delta -= 360.0f;
            while (delta < -180.0f) delta += 360.0f;
            azAngleDeg += delta;
            lastDragAngle = curAngle;
        }

        /* 우클릭: 바늘 현재 각도 기준 미세조정 (밀 단위 스텝, 기어비 반영) */
        if (rMouseDown && !rMouseDownPrev && distToPivot <= leverPivotGrabR) {
            float needleDeg = fmodf(azAngleDeg, 360.0f);
            if (needleDeg > 180.0f) needleDeg -= 360.0f;
            if (needleDeg < -180.0f) needleDeg += 360.0f;
            float clickAngle = atan2f((float)dy, (float)dx) * 180.0f / PI_F;
            float diff = clickAngle - needleDeg;
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;
            float stepDeg = AZ_FINE_STEP_MIL / AZ_MIL_PER_DEG;
            azAngleDeg += (diff < 0.0f) ? stepDeg : -stepDeg; /* 시계방향 클릭=감소, 반시계=증가 */
            leverFlash = LEVER_FLASH_TIME;
        }

        /* 스페이스: 지금 값을 확정 (허용오차 안이든 아니든 일단 확정하고, 최종 판정은 두 값 합산으로) */
        if (keys[VK_SPACE] && !keysPrev[VK_SPACE]) {
            float azCurrentMil = NormalizeMil(azAngleDeg * AZ_MIL_PER_DEG);
            azErrorRecorded = MilDiff(azCurrentMil, (float)azTargetMil);
            transState = TRANS_OUT;
            transTimer = 0.0f;
            draggingLever = FALSE;
        }
    } else { /* FIRESTAGE_ELEVATION -- 세로 릴, 화살표 고정 */
        BOOL inElevationGrabZone = (dx >= -EL_GRAB_HALF_W && dx <= EL_GRAB_HALF_W &&
                                     dy >= -EL_GRAB_HALF_H && dy <= EL_GRAB_HALF_H);
        if (mouseDown && !mouseDownPrev && inElevationGrabZone) {
            draggingLever = TRUE;
            lastDragMouseY = mouseY;
            elVelocity = 0.0f;
        }
        if (draggingLever && !mouseDown) draggingLever = FALSE;

        if (draggingLever) {
            /* 반대 방향: 아래로 끌면 증가, 위로 끌면 감소 */
            int deltaY = mouseY - lastDragMouseY;
            float deltaMil = (float)deltaY * EL_DRAG_MIL_PER_PX;
            elCurrentMil += deltaMil;
            lastDragMouseY = mouseY;
            if (dt > 0.0001f) elVelocity = elVelocity * 0.7f + (deltaMil / dt) * 0.3f;
            if (elCurrentMil < EL_ADJUST_MIN) { elCurrentMil = EL_ADJUST_MIN; elVelocity = 0.0f; }
            if (elCurrentMil > EL_ADJUST_MAX) { elCurrentMil = EL_ADJUST_MAX; elVelocity = 0.0f; }
        } else if (fabsf(elVelocity) > EL_VELOCITY_STOP_MIL) {
            /* 손 뗀 뒤 관성으로 계속 미끄러지다가 마찰로 서서히 멈춤 (휴대폰 슬라이드 느낌) */
            elCurrentMil += elVelocity * dt;
            float decay = 1.0f - EL_FRICTION * dt;
            if (decay < 0.0f) decay = 0.0f;
            elVelocity *= decay;
            if (elCurrentMil < EL_ADJUST_MIN) { elCurrentMil = EL_ADJUST_MIN; elVelocity = 0.0f; }
            if (elCurrentMil > EL_ADJUST_MAX) { elCurrentMil = EL_ADJUST_MAX; elVelocity = 0.0f; }
        } else {
            elVelocity = 0.0f;
        }

        /* 우클릭: 화살표보다 위 클릭하면 증가, 아래 클릭하면 감소 -- 관성도 멈춤 */
        if (rMouseDown && !rMouseDownPrev && inElevationGrabZone) {
            elVelocity = 0.0f;
            elCurrentMil += (dy < 0) ? EL_FINE_STEP_MIL : -EL_FINE_STEP_MIL;
            if (elCurrentMil < EL_ADJUST_MIN) elCurrentMil = EL_ADJUST_MIN;
            if (elCurrentMil > EL_ADJUST_MAX) elCurrentMil = EL_ADJUST_MAX;
            leverFlash = LEVER_FLASH_TIME;
        }

        /* 스페이스: 지금 값을 확정 -- 이게 마지막 스테이지라 최종 판정으로 이어짐 */
        if (keys[VK_SPACE] && !keysPrev[VK_SPACE]) {
            elErrorRecorded = fabsf(elCurrentMil - (float)elTargetMil);
            transState = TRANS_OUT;
            transTimer = 0.0f;
            draggingLever = FALSE;
            elVelocity = 0.0f;
        }
    }
}

static void RenderGameplay(void) {
    if (missionFailed) {
        ClearScreen(0xFF000000);
        const char *msg = "SQUAD WIPED";
        int tw = TextWidth(msg, 4);
        DrawLabel(GAME_W / 2 - tw / 2, GAME_H / 2 - 14, msg, 0xFFFF3030, 4);
        return;
    }

    if (finalResult != RESULT_NONE) {
        ClearScreen(0xFF000000);
        const char *msg = (finalResult == RESULT_HIT) ? "TARGET HIT" : "MISSION FAILED";
        uint32_t col = (finalResult == RESULT_HIT) ? 0xFF3CFF6E : 0xFFFF3030;
        int tw = TextWidth(msg, 4);
        DrawLabel(GAME_W / 2 - tw / 2, GAME_H / 2 - 14, msg, col, 4);
    } else {
        ClearScreen(0xFF161B12); /* bg placeholder */

        /* 좌측 숫자 말풍선 -- 현재 스테이지 목표값(4자리)을 자릿수 단위로 불러줌.
           스테이지가 바뀌면 자동으로 방위각->사각 콜아웃으로 전환된다 (컴포넌트는 계속 유지) */
        int bubbleTarget = (fireStage == FIRESTAGE_AZIMUTH) ? azTargetMil : elTargetMil;
        if (bubblePhase % 2 == 0) {
            FillPixelRect(30, 130, 110, 100, 0xFF6A2540);
            DrawRectOutline(30, 130, 110, 100, 0xFFEEEEEE);
            int digits[4] = {
                (bubbleTarget / 1000) % 10, (bubbleTarget / 100) % 10,
                (bubbleTarget / 10) % 10, bubbleTarget % 10
            };
            DrawNumber(30 + 40, 130 + 40, digits[bubblePhase / 2], 1, 0xFFFFFFFF);
        }

        /* 상단 타이머 게이지 -- 스테이지 전환과 무관하게 계속 흐름 */
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

        /* 중앙 숫자판 */
        FillPixelRect(230, 140, 160, 100, 0xFF1E2A4A);
        DrawRectOutline(230, 140, 160, 100, 0xFFEEEEEE);
        if (fireStage == FIRESTAGE_AZIMUTH) {
            DrawLabel(245, 150, "AZIMUTH MIL", 0xFF88AACC, 1);
            int curMil = (int)NormalizeMil(azAngleDeg * AZ_MIL_PER_DEG);
            DrawNumber(255, 190, curMil, 4, 0xFF3CFF6E);
        } else {
            DrawLabel(240, 150, "ELEVATION MIL", 0xFF88AACC, 1);
            DrawNumber(255, 190, (int)(elCurrentMil + 0.5f), 4, 0xFF3CFF6E);
        }

        /* 우측 조작부 -- 방위각: 여러 바퀴 도는 회전 다이얼 / 사각: 화살표 고정 세로 릴 */
        if (fireStage == FIRESTAGE_AZIMUTH) {
            DrawRing(leverCx, leverCy, 8.0f, 0xFF8A8A9A);
            float visualDeg = fmodf(azAngleDeg, 360.0f);
            float rad = visualDeg * PI_F / 180.0f;
            int hx = leverCx + (int)(cosf(rad) * 60.0f);
            int hy = leverCy + (int)(sinf(rad) * 60.0f);
            for (float t = 0.0f; t <= 1.0f; t += 0.02f) {
                int px = leverCx + (int)((hx - leverCx) * t);
                int py = leverCy + (int)((hy - leverCy) * t);
                FillPixelRect(px - 1, py - 1, 3, 3, (draggingLever || leverFlash > 0.0f) ? 0xFFFFB020 : 0xFF8A8A9A);
            }
            DrawRing(leverCx, leverCy, (float)leverPivotGrabR, 0xFF333333);
        } else {
            /* 드래그 판정 영역(더 넓게)을 먼저 옅게 표시하고, 그 안에 릴 눈금창을 그림 */
            DrawRectOutline(leverCx - EL_GRAB_HALF_W, leverCy - EL_GRAB_HALF_H,
                             EL_GRAB_HALF_W * 2, EL_GRAB_HALF_H * 2, 0xFF262A22);
            int reelX = leverCx, reelTop = leverCy - 130, reelH = 260;
            DrawRectOutline(reelX - 30, reelTop, 60, reelH, 0xFF333333);
            for (int step = -8; step <= 8; step++) {
                int tickMil = ((int)(elCurrentMil) / EL_TICK_STEP + step) * EL_TICK_STEP;
                if (tickMil < 0 || tickMil > (int)EL_ADJUST_MAX) continue;
                float screenY = (float)leverCy - ((float)tickMil - elCurrentMil) * EL_PX_PER_MIL;
                if (screenY < (float)reelTop - 10.0f || screenY > (float)(reelTop + reelH) + 10.0f) continue;
                DrawNumber(reelX - 24, (int)screenY - 8, tickMil, 4, 0xFF8FB0D0);
            }
            DrawArrowRight(reelX - 34, leverCy, 10, (draggingLever || leverFlash > 0.0f) ? 0xFFFFB020 : 0xFFEEEEEE);
        }

        /* 하단 단축키 안내 -- 두 스테이지 공통, 계속 유지 */
        DrawLabel(20, GAME_H - 32, "DRAG: COARSE", 0xFFAAAAAA, 1);
        DrawLabel(20, GAME_H - 20, "R-CLICK: FINE-ADJUST", 0xFFAAAAAA, 1);
        DrawLabel(200, GAME_H - 20, "SPACE: CONFIRM", 0xFFAAAAAA, 1);
        int escW = TextWidth("ESC: QUIT", 1);
        DrawLabel(GAME_W - 20 - escW, GAME_H - 20, "ESC: QUIT", 0xFFAAAAAA, 1);
    }

    /* 스테이지/결과 전환 시 검정화면 페이드 인/아웃 (결과 화면도 이걸로 페이드인됨) */
    if (transState == TRANS_OUT) {
        FillPixelRectAlpha(0, 0, GAME_W, GAME_H, 0x000000, transTimer / TRANS_TIME);
    } else if (transState == TRANS_IN) {
        FillPixelRectAlpha(0, 0, GAME_W, GAME_H, 0x000000, 1.0f - (transTimer / TRANS_TIME));
    }
}

/* ---------- 메인 업데이트/렌더 ---------- */

static void UpdateGame(float dt) {
    switch (scene) {
        case SCENE_INTRO: UpdateIntro(dt); break;
        case SCENE_GAMEPLAY: UpdateGameplay(dt); break;
        default: break;
    }
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
        case WM_RBUTTONDOWN:
            rMouseDown = TRUE;
            return 0;
        case WM_RBUTTONUP:
            rMouseDown = FALSE;
            return 0;
        default: return DefWindowProc(hwnd, msg, wp, lp);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)hPrev; (void)cmdLine;
    srand(GetTickCount());

    /* Windows 기본 타이머 해상도(보통 ~15ms)에서는 Sleep(1)이 실제로 훨씬 오래 걸려
       프레임이 낮아 보인다. 1ms 해상도를 요청해서 프레임 페이싱을 촘촘하게 만든다.
       실행파일 크기엔 영향 없음(가져오는 함수 몇 개 추가되는 수준). */
    timeBeginPeriod(1);

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
        /* 이번 프레임 입력을 받기 전에 "이전 프레임" 상태를 스냅샷 -- Update/Render
           양쪽 어디서 엣지(방금 눌림)를 체크하든 동일한 기준을 보게 하기 위함 */
        for (int i = 0; i < 256; i++) keysPrev[i] = keys[i];
        mouseDownPrev = mouseDown;
        rMouseDownPrev = rMouseDown;

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
    timeEndPeriod(1);
    return 0;
}
