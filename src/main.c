/*
 * 1.44MB 게임 콘테스트용 화면 골격(스캐폴드) 프로토타입.
 * 로비 -> 인트로 -> 조작화면(3단계: 방위각/사각/사격) (+ 옵션 오버레이) 전체 흐름.
 * 실제 아트는 전부 나중에 이미지로 교체할 자리 -- 지금은 색깔로만 구분한 사각형.
 * 텍스트는 전부 한글 -- 손으로 그린 5x7 폰트로는 한글이 불가능해서(조합형 11,172자),
 * 프레임버퍼를 화면에 StretchDIBits로 그린 "다음"에 같은 HDC 위에 GDI TextOutW/DrawTextW로
 * 시스템 폰트(맑은 고딕)를 직접 덧그리는 방식을 쓴다 (외부 폰트 파일 없음, 용량 영향 없음).
 * 문자열은 소스가 UTF-8이라 MultiByteToWideChar로 그때그때 변환해서 넘긴다 (Kor() 참고).
 */

#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
static int langIndex = 0; /* 0=한국어 1=영어 2=일본어 -- 일본어는 아직 미번역이라 한국어로 폴백(T() 참고) */
static const char *LANG_NAMES[3] = { "한국어", "영어", "일본어" };
static const char *LANG_NAMES_EN[3] = { "Korean", "English", "Japanese" };

static float introTimer = 0.0f;

static uint32_t lobbyBgPixels[GAME_W * GAME_H];
static BOOL lobbyBgLoaded = FALSE;

#define LOGO_MAX_W 400
#define LOGO_MAX_H 100
#define LOGO_X 15
#define LOGO_Y 18
#define LOGO_CHROMA_KEY 0xFFFF00FF  /* 로고 배경의 마젠타를 투명 처리 */
#define LOGO_ANIM_TIME 0.45f        /* 로비 진입 시 페이드인 + 살짝 아래에서 올라오는 시간 */
#define LOGO_RISE_PX 4.0f
static uint32_t logoPixels[LOGO_MAX_W * LOGO_MAX_H];
static BOOL logoLoaded = FALSE;
static int logoW = 0, logoH = 0;
static BOOL lobbyWasActive = FALSE;  /* 방금 로비(또는 옵션)에 막 들어왔는지 감지용 */
static float lobbyLogoTimer = 0.0f;

/* ---------- 포병 밀(Mil) 단위 사격제원 ---------- */
/* 원 한 바퀴 = 6400밀. 방위각은 여러 바퀴 돌려서 맞추고, 사각은 -44~1244밀 범위의
   세로 릴로 맞춘다 (음수 구간은 이번 프로토타입에서 스킵 -- 실사용 범위 300~1100 위주). */
#define MIL_FULL 6400.0f
#define AZ_FINE_STEP_MIL 1.0f
#define AZ_GEAR_TURNS 15.0f  /* 핸들을 최대 15바퀴 돌려야 전체 범위(6400밀)를 커버 -- 원래 20이었는데 너무 많다는 피드백으로 완화 */
#define AZ_MIL_PER_DEG (MIL_FULL / AZ_GEAR_TURNS / 360.0f)

#define EL_TARGET_MIN 300
#define EL_TARGET_MAX 1100
#define EL_ADJUST_MIN 0.0f
#define EL_ADJUST_MAX 1244.0f
#define EL_FINE_STEP_MIL 1.0f
#define EL_DRAG_MIL_PER_PX 0.5f     /* 릴 드래그 민감도. 4 -> 1 -> 0.5로 계속 완화(여전히 살짝만 끌어도 많이 움직인다는 피드백) */
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
/* 4단계(장약)는 문장이 통째로 뜨고 사라지는 방식이라 다른 스테이지의 4자리 콜아웃과
   달리 2단계(표시-대기)만 씀: 1초 유지 후 사라지고, 3초 쉬었다가 다시 표시 + 사운드.
   너무 자주 뜨면 시끄러워서 대기시간을 길게 둠. */
#define CHARGE_BUBBLE_SHOW_TIME 1.0f
#define CHARGE_BUBBLE_HIDE_TIME 3.0f
#define CHARGE_BUBBLE_PHASE_COUNT 2
static const float BUBBLE_PHASE_DUR_CHARGE[CHARGE_BUBBLE_PHASE_COUNT] = {
    CHARGE_BUBBLE_SHOW_TIME, CHARGE_BUBBLE_HIDE_TIME
};

/* 각 스테이지 진입 직후 딱 한 번, 말풍선이 핑크 배경째로 빠르게 3번 깜빡이는 "치직" 연출.
   이 연출이 끝난 뒤에야 위 BUBBLE_PHASE_DUR 자릿수 콜아웃이 시작된다. */
#define BUBBLE_INTRO_FLASH_TIME 0.08f
#define BUBBLE_INTRO_FLASH_COUNT 6  /* 켜짐/꺼짐 3회씩 = 총 6단계 */

/* ---------- 3단계: 사격 타이밍 ---------- */
#define FIRE_HITS_REQUIRED 5
#define FIRE_MARK_TOLERANCE_PX 9.0f
#define FIRE_BULLET_SPEED_BASE 70.0f   /* 게이지 폭 기준 px/sec */
#define FIRE_BULLET_SPEED_STEP 14.0f   /* 명중마다 점점 빨라짐 */
#define FIRE_GAUGE_X 470
#define FIRE_GAUGE_Y 170
#define FIRE_GAUGE_W 130
#define FIRE_GAUGE_H 22
#define FIRE_SQUARE_START_X 380
#define FIRE_SQUARE_MIN_X 40
#define FIRE_SQUARE_Y 155
#define FIRE_SQUARE_SIZE 40

/* ---------- 4단계: 장약 고르기 ---------- */
/* 번호(1~5)가 적힌 원반 20개가 무더기로 쌓여있다. 드래그로 하나씩 치워가며(z 순서가
   집을 때마다 맨 위로 올라감) 무전으로 지정된 번호를 찾아, 화살표가 위아래로 움직이는
   통에 끌어다 넣으면 완료. 정답은 "정확히 하나"가 아니라 "통 안 원반들의 합"으로
   판정한다 -- 예를 들어 7이 목표면 7 하나를 넣어도, 3+4를 넣어도, 1을 7개 넣어도 됨.
   엉뚱한 조합을 넣어도 페널티는 없음(그냥 안 끝날 뿐) -- 순수 탐색+합산 퍼즐. */
#define CHARGE_COUNT 20
#define CHARGE_RADIUS 17
#define CHARGE_PILE_CX 390
#define CHARGE_PILE_CY 195
#define CHARGE_PILE_SPREAD_X 70
#define CHARGE_PILE_SPREAD_Y 65
#define CHARGE_BIN_X 560
#define CHARGE_BIN_Y 110
#define CHARGE_BIN_W 60
#define CHARGE_BIN_H 170
#define CHARGE_BUBBLE_X 20   /* 장약 콜아웃은 문장이라 기본 말풍선(30,110폭)보다 넓게 필요 */
#define CHARGE_BUBBLE_Y 130
#define CHARGE_BUBBLE_W 240
#define CHARGE_BUBBLE_H 100
#define CHARGE_TARGET_MIN 10  /* 원반 값(1~5)만으로는 절대 한 방에 안 맞아서 항상 여러 개를 조합해야 함 */
#define CHARGE_TARGET_MAX 18
#define CHARGE_DISK_MIN 1
#define CHARGE_DISK_MAX 5

typedef enum { FIRESTAGE_AZIMUTH, FIRESTAGE_ELEVATION, FIRESTAGE_FIRE, FIRESTAGE_CHARGE } FireStage;
typedef enum { TRANS_NONE, TRANS_OUT, TRANS_IN } TransState;
typedef enum { RESULT_NONE, RESULT_HIT, RESULT_MISS, RESULT_FIRE_FAIL } FinalResult;

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
static BOOL bubbleIntroActive = FALSE;  /* 스테이지 진입 직후 "치직" 깜빡임 구간인지 */
static int bubbleIntroPhase = 0;
static float bubbleIntroTimer = 0.0f;

static float leverFlash = 0.0f;
static BOOL draggingLever = FALSE;
static float lastDragAngle = 0.0f;
static int lastDragMouseY = 0;
static int azLastTickMil = -1;   /* 방위각 틱 사운드 기준점(1밀 넘어갈 때마다 재생) */
static int elLastTickMil = -1;   /* 사각 틱 사운드 기준점 */

static BOOL missionFailed = FALSE;
static float failTimer = 0.0f;

static float azErrorRecorded = 0.0f;
static float elErrorRecorded = 0.0f;
static FinalResult finalResult = RESULT_NONE;
static float resultTimer = 0.0f;

/* 3단계 상태 */
static int fireHits = 0;
static float fireBulletPos = 0.0f;   /* 0..1, 게이지 내 정규화 위치 */
static float fireBulletDir = 1.0f;
static float fireBulletSpeed = FIRE_BULLET_SPEED_BASE;
static float fireMarkPos = 0.5f;     /* 0..1, 붉은 표시 위치 */
static BOOL fireStageFailed = FALSE;
static float fireSquareX = (float)FIRE_SQUARE_START_X;

/* 4단계 상태 */
static int chargeNumber[CHARGE_COUNT];   /* 각 원반에 적힌 번호(1~5) */
static float chargeX[CHARGE_COUNT], chargeY[CHARGE_COUNT];
static int chargeZ[CHARGE_COUNT];        /* 쌓인 순서 -- 클수록 위, 집으면 맨 위로 갱신 */
static int chargeNextZ = 0;
static int chargeTargetNumber = 1;       /* 이번 라운드에 찾아야 하는 번호 */
static int chargeDragIdx = -1;           /* 지금 드래그 중인 원반 인덱스, 없으면 -1 */

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
static void DrawArrowDown(int tipX, int tipY, int size, uint32_t color) {
    for (int i = 0; i < size; i++) {
        int rowW = (size - i) * 2;
        FillPixelRect(tipX - rowW / 2, tipY - size + i, rowW, 1, color);
    }
}
static void FillCircle(int cx, int cy, int radius, uint32_t color) {
    for (int y = -radius; y <= radius; y++) {
        int rowW = (int)sqrtf((float)(radius * radius - y * y));
        FillPixelRect(cx - rowW, cy + y, rowW * 2 + 1, 1, color);
    }
}
static void DrawGearIcon(int cx, int cy, int radius, uint32_t color) {
    /* 톱니 -- 원 몸통 둘레에 작은 사각형 8개를 방사형으로 배치 */
    for (int i = 0; i < 8; i++) {
        float ang = (float)i * (PI_F / 4.0f);
        int tx = cx + (int)(cosf(ang) * (float)radius);
        int ty = cy + (int)(sinf(ang) * (float)radius);
        FillPixelRect(tx - 3, ty - 3, 6, 6, color);
    }
    FillCircle(cx, cy, radius - 3, color);
}

/* ---------- BMP 로더 (배경 아트용) ----------
   외부 이미지 파일(images/*.bmp)을 읽어 GAME_W x GAME_H 픽셀 배열로 채운다.
   8비트 인덱스(팔레트) 또는 24비트 무압축(BI_RGB) BMP만 지원 -- 포토샵에서
   바로 뽑을 수 있는 형식이라 별도 변환 없이 씀. 리사이즈는 안 하므로 원본이
   정확히 GAME_W x GAME_H(640x360)이어야 함. 실행파일 옆 images 폴더에서 찾는다. */

static void GetAssetPath(char *outPath, size_t cap, const char *relPath) {
    char exeDir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exeDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { exeDir[0] = '\0'; }
    char *lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0'; else exeDir[0] = '\0';
    wsprintfA(outPath, "%s%s", exeDir, relPath);
    (void)cap;
}

/* expectW/expectH > 0 이면 그 크기와 정확히 일치해야 함(배경용). expectW==0이면 파일에
   적힌 크기를 그대로 쓰되 maxW/maxH를 넘으면 실패(스프라이트용 -- 버퍼 오버플로 방지).
   실제 로드된 크기는 outW/outH로 돌려줌(둘 다 NULL 가능). 8/16/24bpp 무압축(BI_RGB) 지원 --
   16bpp는 비트마스크 없는 기본 포맷인 X1R5G5B5로 해석함(포토샵 "16 Bit" 내보내기가 이 형식). */
static BOOL LoadBmpInto(const char *path, uint32_t *outPixels, int expectW, int expectH,
                         int maxW, int maxH, int *outW, int *outH) {
    FILE *f = fopen(path, "rb");
    if (!f) return FALSE;

    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    BOOL headerOk = fread(&fh, sizeof(fh), 1, f) == 1 && fh.bfType == 0x4D42 &&
                     fread(&ih, sizeof(ih), 1, f) == 1 &&
                     ih.biCompression == BI_RGB &&
                     (ih.biBitCount == 8 || ih.biBitCount == 16 || ih.biBitCount == 24);
    if (!headerOk) { fclose(f); return FALSE; }

    int w = ih.biWidth;
    int h = (ih.biHeight < 0) ? -ih.biHeight : ih.biHeight;
    if (expectW > 0 && (w != expectW || h != expectH)) { fclose(f); return FALSE; }
    if (maxW > 0 && (w > maxW || h > maxH)) { fclose(f); return FALSE; }
    if (outW) *outW = w;
    if (outH) *outH = h;

    BOOL topDown = (ih.biHeight < 0);
    int bpp = ih.biBitCount;

    static uint32_t palette[256];
    if (bpp == 8) {
        int numColors = ih.biClrUsed ? (int)ih.biClrUsed : 256;
        if (numColors > 256) numColors = 256;
        fseek(f, (long)(sizeof(fh) + ih.biSize), SEEK_SET);
        for (int i = 0; i < numColors; i++) {
            unsigned char bgra[4];
            if (fread(bgra, 4, 1, f) != 1) { fclose(f); return FALSE; }
            palette[i] = 0xFF000000 | ((uint32_t)bgra[2] << 16) | ((uint32_t)bgra[1] << 8) | bgra[0];
        }
    }

    int rowStride = ((w * bpp / 8) + 3) & ~3;
    static unsigned char rowBuf[GAME_W * 3 + 4]; /* 24bpp 640폭 기준 최대 행 크기면 충분 */
    if (rowStride > (int)sizeof(rowBuf)) { fclose(f); return FALSE; }

    fseek(f, (long)fh.bfOffBits, SEEK_SET);
    for (int rowIdx = 0; rowIdx < h; rowIdx++) {
        if (fread(rowBuf, (size_t)rowStride, 1, f) != 1) { fclose(f); return FALSE; }
        int y = topDown ? rowIdx : (h - 1 - rowIdx);
        for (int x = 0; x < w; x++) {
            uint32_t px;
            if (bpp == 8) {
                px = palette[rowBuf[x]];
            } else if (bpp == 16) {
                unsigned short p16 = (unsigned short)(rowBuf[x * 2] | (rowBuf[x * 2 + 1] << 8));
                int r5 = (p16 >> 10) & 0x1F, g5 = (p16 >> 5) & 0x1F, b5 = p16 & 0x1F;
                int r = (r5 << 3) | (r5 >> 2), g = (g5 << 3) | (g5 >> 2), b = (b5 << 3) | (b5 >> 2);
                px = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            } else {
                unsigned char b = rowBuf[x * 3 + 0], g = rowBuf[x * 3 + 1], r = rowBuf[x * 3 + 2];
                px = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
            outPixels[y * w + x] = px;
        }
    }
    fclose(f);
    return TRUE;
}

/* 마젠타(등) 키컬러 픽셀은 건너뛰고, 나머지는 alpha 비율로 프레임버퍼와 블렌딩해서 그린다
   (로고 페이드인 애니메이션용 -- alpha 1.0이면 완전 불투명하게 그대로 그려짐) */
static void BlitSpriteAlpha(const uint32_t *src, int w, int h, int destX, int destY,
                             uint32_t keyColor, float alpha) {
    if (alpha <= 0.0f) return;
    if (alpha > 1.0f) alpha = 1.0f;
    uint32_t keyRGB = keyColor & 0x00FFFFFF;
    for (int y = 0; y < h; y++) {
        int dy = destY + y;
        if (dy < 0 || dy >= GAME_H) continue;
        for (int x = 0; x < w; x++) {
            uint32_t px = src[y * w + x];
            if ((px & 0x00FFFFFF) == keyRGB) continue;
            int dx = destX + x;
            if (dx < 0 || dx >= GAME_W) continue;
            uint32_t old = framebuffer[dy * GAME_W + dx];
            int orr = (int)((old >> 16) & 0xFF), og = (int)((old >> 8) & 0xFF), ob = (int)(old & 0xFF);
            int sr = (int)((px >> 16) & 0xFF), sg = (int)((px >> 8) & 0xFF), sb = (int)(px & 0xFF);
            int r = (int)((float)orr * (1.0f - alpha) + (float)sr * alpha);
            int g = (int)((float)og * (1.0f - alpha) + (float)sg * alpha);
            int b = (int)((float)ob * (1.0f - alpha) + (float)sb * alpha);
            framebuffer[dy * GAME_W + dx] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

/* ---------- 한글 텍스트 (프레임버퍼 위에 GDI로 후처리 렌더링) ----------
   프레임버퍼는 순수 픽셀 배열이라 여기 직접 한글을 그릴 방법이 없다. 대신 이번 프레임에
   그릴 텍스트를 큐에 모아뒀다가, StretchDIBits로 프레임버퍼를 그린 "바로 다음"에 같은
   DC 위에 TextOutW/DrawTextW로 시스템 폰트(맑은 고딕)를 덧그린다.
   단, 이 두 단계를 화면 DC에 직접 하면 그 사이 순간이 화면에 노출되어 텍스트가
   깜빡여 보인다 -- 그래서 오프스크린 메모리 DC(g_hdc)에 두 단계를 다 그린 뒤
   마지막에 BitBlt로 화면에 한 번에 옮긴다 (더블 버퍼링). */

static wchar_t g_korBuf[4][128];
static int g_korBufIdx = 0;
static const wchar_t *Kor(const char *utf8) {
    wchar_t *buf = g_korBuf[g_korBufIdx];
    g_korBufIdx = (g_korBufIdx + 1) % 4;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buf, 128);
    return buf;
}

/* 언어 전환용: langIndex==1(영어)이면 영문을, 그 외(한국어/아직 미번역인 일본어)는
   한국어 원문을 고른다. 영문도 순수 ASCII라 UTF-8 취급해도 문제없어서 Kor()를
   그대로 재사용(회전버퍼 4칸 규칙도 Kor()와 동일하게 적용됨). */
static const wchar_t *T(const char *kor, const char *eng) {
    return Kor((langIndex == 1) ? eng : kor);
}

#define TEXT_QUEUE_CAP 48
typedef struct {
    int x, y, w, h;   /* w==0 이면 (x,y)를 좌상단으로 그대로 출력, 아니면 rect 안에서 가운데 정렬 */
    wchar_t text[96];
    uint32_t color;
    int fontIdx;      /* 0=작게 1=중간 2=크게 */
} TextCmd;
static TextCmd textQueue[TEXT_QUEUE_CAP];
static int textQueueCount = 0;
static BOOL cursorHot = FALSE;   /* 지금 마우스 아래가 클릭/드래그 가능한 지점인지 (커서 색 결정용) */
static BOOL inputBlocked = FALSE; /* 옵션 팝업이 떠 있을 때 뒤쪽 화면 입력 차단용 */

static HDC g_hdc;         /* 오프스크린 메모리 DC -- 프레임버퍼+텍스트를 여기 다 그린 뒤 화면에 한 번에 BitBlt */
static HDC g_screenDC;    /* 실제 화면(창) DC -- BitBlt 대상 */
static HBITMAP g_memBitmap, g_memBitmapOld;
static HFONT g_fontSmall, g_fontMedium, g_fontLarge;

/* ---------- 커스텀 한글 폰트(HBIOS-SYS, 서브셋) -- 설치 없이 프로세스 전용으로만 등록 ----------
   fonts/HBIOS-SYS.ttf(게임에 실제 쓰이는 글자만 남긴 서브셋, 라이선스는 fonts/LICENSE-HBIOS-SYS.txt
   참고)를 실행 중에만 AddFontMemResourceEx로 등록해서 CreateFontW에서 이름으로 골라 쓸 수 있게
   한다. 로드 실패하면 기존 시스템 폰트(맑은 고딕)로 자동 폴백. */
#define CUSTOM_FONT_MAX_SIZE 65536
static unsigned char g_customFontData[CUSTOM_FONT_MAX_SIZE];
static HANDLE g_customFontRes = NULL;
static BOOL g_customFontLoaded = FALSE;

static BOOL LoadCustomFont(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return FALSE;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > CUSTOM_FONT_MAX_SIZE) { fclose(f); return FALSE; }
    size_t rd = fread(g_customFontData, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) return FALSE;
    DWORD numFonts = 0;
    g_customFontRes = AddFontMemResourceEx(g_customFontData, (DWORD)sz, NULL, &numFonts);
    return (g_customFontRes != NULL && numFonts > 0);
}

static void QueueTextPoint(int x, int y, int fontIdx, uint32_t color, const wchar_t *text) {
    if (textQueueCount >= TEXT_QUEUE_CAP) return;
    TextCmd *c = &textQueue[textQueueCount++];
    c->x = x; c->y = y; c->w = 0; c->h = 0; c->fontIdx = fontIdx; c->color = color;
    lstrcpynW(c->text, text, 96);
}
static void QueueTextCentered(int x, int y, int w, int h, int fontIdx, uint32_t color, const wchar_t *text) {
    if (textQueueCount >= TEXT_QUEUE_CAP) return;
    TextCmd *c = &textQueue[textQueueCount++];
    c->x = x; c->y = y; c->w = w; c->h = h; c->fontIdx = fontIdx; c->color = color;
    lstrcpynW(c->text, text, 96);
}

/* ---------- 7세그먼트 숫자 (게이지/큰 숫자용 -- 한글과 무관하게 계속 직접 그림) ---------- */

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

/* ---------- 오디오: waveOut으로 직접 합성하는 "지직" 잡음 (외부 사운드 파일 없음) ---------- */

#define STATIC_SND_RATE 8000
#define STATIC_SND_SAMPLES (STATIC_SND_RATE * 4 / 10) /* 0.4초 */

static HWAVEOUT g_waveOut = NULL;
static WAVEHDR g_waveHdr;
static unsigned char g_staticBuf[STATIC_SND_SAMPLES];
static BOOL g_waveOutOpen = FALSE;

static void InitAudio(void) {
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = STATIC_SND_RATE;
    wfx.wBitsPerSample = 8;
    wfx.nBlockAlign = 1;
    wfx.nAvgBytesPerSec = STATIC_SND_RATE;
    if (waveOutOpen(&g_waveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
        g_waveOutOpen = TRUE;
    }
}

static void PlayStaticNoise(void) {
    if (!soundOn || !g_waveOutOpen) return;
    waveOutReset(g_waveOut);
    if (g_waveHdr.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_waveOut, &g_waveHdr, sizeof(WAVEHDR));

    /* 백색 잡음 + 이따금 튀는 팝(pop)으로 라디오 크랙클 느낌을 낸다. 뒤로 갈수록 서서히 잦아듦. */
    for (int i = 0; i < STATIC_SND_SAMPLES; i++) {
        int n = rand() % 256;
        if ((rand() % 100) < 15) n = (rand() % 2) ? 255 : 0;
        float envelope = 1.0f - (float)i / (float)STATIC_SND_SAMPLES * 0.3f;
        int centered = (int)(((float)n - 128.0f) * envelope * 0.2f) + 128;  /* 0.5 -> 0.2로 추가 축소 (여전히 크다는 피드백) */
        if (centered < 0) centered = 0;
        if (centered > 255) centered = 255;
        g_staticBuf[i] = (unsigned char)centered;
    }
    ZeroMemory(&g_waveHdr, sizeof(WAVEHDR));
    g_waveHdr.lpData = (LPSTR)g_staticBuf;
    g_waveHdr.dwBufferLength = STATIC_SND_SAMPLES;
    waveOutPrepareHeader(g_waveOut, &g_waveHdr, sizeof(WAVEHDR));
    waveOutWrite(g_waveOut, &g_waveHdr, sizeof(WAVEHDR));
}

/* ---------- 조작 틱/명중 사운드: 레버·릴 드래그 중 "띠리릭" 틱음 + 3단계 명중음 ----------
   둘 다 짧고 자주 재생될 수 있어서 SFX/BGM/War와는 또 다른 전용 waveOut 핸들을 씀. */
#define TICK_MIL_STEP 10   /* 1밀마다 재생하면 빠르게 돌릴 때 재트리거가 너무 잦아서 음이 겹쳐
                               높아지는 것처럼 들림 -- 10밀마다 한 번으로 완화 */
#define TICK_SAMPLE_RATE 8000
#define TICK_SAMPLES (TICK_SAMPLE_RATE * 25 / 1000)  /* 25ms 짧은 클릭 */
#define HIT_SAMPLES (TICK_SAMPLE_RATE * 12 / 100)    /* 120ms 상승 차임 */

static HWAVEOUT g_waveOutTick = NULL;
static WAVEHDR g_tickHdr;
static unsigned char g_tickBuf[TICK_SAMPLES];
static unsigned char g_hitBuf[HIT_SAMPLES];
static BOOL g_tickOutOpen = FALSE;

static void InitTick(void) {
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = TICK_SAMPLE_RATE;
    wfx.wBitsPerSample = 8;
    wfx.nBlockAlign = 1;
    wfx.nAvgBytesPerSec = TICK_SAMPLE_RATE;
    if (waveOutOpen(&g_waveOutTick, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
        g_tickOutOpen = TRUE;
    }
}

/* 레버/릴이 1밀 단위로 넘어갈 때마다 짧게 딸깍 -- 빠르게 돌리면 자연히 "띠리릭"처럼 연속으로 들림 */
static void PlayTick(void) {
    if (!soundOn || !g_tickOutOpen) return;
    waveOutReset(g_waveOutTick);
    if (g_tickHdr.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_waveOutTick, &g_tickHdr, sizeof(WAVEHDR));

    float freq = 950.0f;
    for (int i = 0; i < TICK_SAMPLES; i++) {
        float t = (float)i / (float)TICK_SAMPLES;
        float env = expf(-t * 20.0f);
        float s = sinf(2.0f * PI_F * freq * (float)i / (float)TICK_SAMPLE_RATE);
        int v = 128 + (int)(s * env * 30.0f);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        g_tickBuf[i] = (unsigned char)v;
    }
    ZeroMemory(&g_tickHdr, sizeof(WAVEHDR));
    g_tickHdr.lpData = (LPSTR)g_tickBuf;
    g_tickHdr.dwBufferLength = TICK_SAMPLES;
    waveOutPrepareHeader(g_waveOutTick, &g_tickHdr, sizeof(WAVEHDR));
    waveOutWrite(g_waveOutTick, &g_tickHdr, sizeof(WAVEHDR));
}

/* 3단계 명중 시 망치로 때린 듯한 타격음 -- 틱과 같은 핸들을 재사용(동시에 울릴 일이 없음:
   드래그 중엔 3단계에 없고, 3단계에선 드래그를 안 함). 상승하는 맑은 차임이었던 걸
   "캐쥬얼하다"는 피드백으로 교체 -- 노이즈 트랜지언트(타격 순간 "탁") + 낮은 통울림
   바디(둔탁한 저음 "퉁") + 짧은 금속성 배음(쇠붙이 느낌 살짝) 3레이어를 섞음. */
static void PlayHitSound(void) {
    if (!soundOn || !g_tickOutOpen) return;
    waveOutReset(g_waveOutTick);
    if (g_tickHdr.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_waveOutTick, &g_tickHdr, sizeof(WAVEHDR));

    for (int i = 0; i < HIT_SAMPLES; i++) {
        float t = (float)i / (float)HIT_SAMPLES;         /* 0..1 진행률(엔벨로프용) */
        float ph = (float)i / (float)TICK_SAMPLE_RATE;   /* 경과 시간(초, 위상 계산용) */

        /* 타격 트랜지언트: 아주 짧게(~10ms) 감쇠하는 노이즈 -- 망치가 부딪히는 순간의 "탁" */
        float noiseEnv = expf(-t * 45.0f);
        float noise = ((float)(rand() % 2001 - 1000) / 1000.0f) * noiseEnv;

        /* 낮은 통울림(바디): 110Hz가 천천히 감쇠 -- 망치머리가 때리는 둔탁한 저음 "퉁" */
        float bodyEnv = expf(-t * 6.0f);
        float body = sinf(2.0f * PI_F * 110.0f * ph) * bodyEnv;

        /* 짧고 조용한 금속성 배음 -- 쇠붙이끼리 부딪히는 느낌만 살짝 */
        float ringEnv = expf(-t * 20.0f);
        float ring = sinf(2.0f * PI_F * 950.0f * ph) * ringEnv;

        float s = noise * 0.5f + body * 1.0f + ring * 0.15f;
        int v = 128 + (int)(s * 240.0f); /* 80 -> 240 (3배): "타격감이 약하다"는 피드백으로 증폭 */
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        g_hitBuf[i] = (unsigned char)v;
    }
    ZeroMemory(&g_tickHdr, sizeof(WAVEHDR));
    g_tickHdr.lpData = (LPSTR)g_hitBuf;
    g_tickHdr.dwBufferLength = HIT_SAMPLES;
    waveOutPrepareHeader(g_waveOutTick, &g_tickHdr, sizeof(WAVEHDR));
    waveOutWrite(g_waveOutTick, &g_tickHdr, sizeof(WAVEHDR));
}

/* ---------- 무전 콜아웃 보이스: 실제 육성 녹음(0~9) -- 밴드패스+링모듈레이션+비트크러시로
   가공한 샘플을 audio/voice_digits.bin에서 읽어와 재생. 방위각/사각/장약 숫자를 순서대로
   불러줄 때 사용. waveOut은 waveOutWrite를 연달아 호출하면 버퍼들을 자동으로 순서대로
   이어 재생해주므로(드라이버 큐), 여러 자리 숫자를 헤더 여러 개로 큐잉하면 됨. */
#define VOICE_SAMPLE_RATE 8000
#define VOICE_DIGIT_COUNT 10
#define VOICE_MAX_LEN 6000   /* 실측 최대 4186바이트, 재녹음 대비 여유 확보 */
#define VOICE_MAX_DIGITS 4   /* 한 번에 이어 재생할 최대 자리수 (장약=2자리, 방위각/사각=1자리씩) */

static unsigned char g_voiceDigitData[VOICE_DIGIT_COUNT][VOICE_MAX_LEN];
static int g_voiceDigitLen[VOICE_DIGIT_COUNT];
static BOOL g_voiceLoaded = FALSE;

static HWAVEOUT g_waveOutVoice = NULL;
static BOOL g_voiceOutOpen = FALSE;
static WAVEHDR g_voiceHdrs[VOICE_MAX_DIGITS];

/* audio/voice_digits.bin 포맷: (uint32 길이 + PCM 데이터) x 10, 인덱스=숫자값(0~9) 순서 */
static BOOL LoadVoiceDigits(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return FALSE;
    for (int i = 0; i < VOICE_DIGIT_COUNT; i++) {
        uint32_t len;
        if (fread(&len, sizeof(len), 1, f) != 1) { fclose(f); return FALSE; }
        if (len > VOICE_MAX_LEN) { fclose(f); return FALSE; }
        if (fread(g_voiceDigitData[i], 1, len, f) != len) { fclose(f); return FALSE; }
        g_voiceDigitLen[i] = (int)len;
    }
    fclose(f);
    return TRUE;
}

static void InitVoice(void) {
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = VOICE_SAMPLE_RATE;
    wfx.wBitsPerSample = 8;
    wfx.nBlockAlign = 1;
    wfx.nAvgBytesPerSec = VOICE_SAMPLE_RATE;
    if (waveOutOpen(&g_waveOutVoice, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
        g_voiceOutOpen = TRUE;
    }
}

/* 여러 자리 숫자를 순서대로 이어 재생 (예: {1,8} -> "일" 다음 "팔") */
static void PlayVoiceDigits(const int *digits, int count) {
    if (!soundOn || !g_voiceOutOpen || !g_voiceLoaded) return;
    waveOutReset(g_waveOutVoice);
    for (int i = 0; i < VOICE_MAX_DIGITS; i++) {
        if (g_voiceHdrs[i].dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(g_waveOutVoice, &g_voiceHdrs[i], sizeof(WAVEHDR));
        }
        ZeroMemory(&g_voiceHdrs[i], sizeof(WAVEHDR));
    }
    if (count > VOICE_MAX_DIGITS) count = VOICE_MAX_DIGITS;
    for (int i = 0; i < count; i++) {
        int d = digits[i];
        if (d < 0 || d > 9) continue;
        g_voiceHdrs[i].lpData = (LPSTR)g_voiceDigitData[d];
        g_voiceHdrs[i].dwBufferLength = (DWORD)g_voiceDigitLen[d];
        waveOutPrepareHeader(g_waveOutVoice, &g_voiceHdrs[i], sizeof(WAVEHDR));
        waveOutWrite(g_waveOutVoice, &g_voiceHdrs[i], sizeof(WAVEHDR));
    }
}

static void PlayVoiceDigit(int d) {
    PlayVoiceDigits(&d, 1);
}

/* value를 앞자리부터 numDigits자리로 분해해서 이어 읽음 (0으로 패딩되는 자리도 포함해서
   그대로 읽음 -- 실제 포병처럼 "0245"도 앞자리 0을 생략하지 않고 그대로 읽는 방식과 동일) */
static void PlayVoiceNumber(int value, int numDigits) {
    int digits[VOICE_MAX_DIGITS];
    if (numDigits > VOICE_MAX_DIGITS) numDigits = VOICE_MAX_DIGITS;
    int v = value;
    for (int i = numDigits - 1; i >= 0; i--) {
        digits[i] = v % 10;
        v /= 10;
    }
    PlayVoiceDigits(digits, numDigits);
}

/* ---------- BGM: 칩튠 베이스라인도 코드로 직접 합성 (오디오 파일 0개 원칙 유지) ----------
   waveOut 핸들을 SFX(g_waveOut)와 별도로 하나 더 열어서(g_waveOutMusic) 지직 잡음이랑
   동시에 재생돼도 서로 안 끊기게 한다. 루프는 한 번에 다 렌더링해둔 버퍼를
   WHDR_DONE 될 때마다 다시 큐에 넣는 방식(수동 반복) -- 매 프레임 폴링. */

#define MUSIC_SAMPLE_RATE 8000
#define MUSIC_BPM 72  /* 92 -> 72로 늦춤(더 무겁게 끄는 행군) -- 정수 연산으로만 계산 */
#define MUSIC_STEP_SAMPLES (MUSIC_SAMPLE_RATE * 60 / MUSIC_BPM / 4) /* 16분음표 */
#define MUSIC_QUARTER_SAMPLES (MUSIC_STEP_SAMPLES * 4)
#define MUSIC_STEPS 64  /* 16분음표 x 64 = 4마디 */
#define MUSIC_TOTAL_SAMPLES (MUSIC_STEP_SAMPLES * MUSIC_STEPS)

/* 참고 트랙(war.mp3, Suno로 만든 21초짜리 전쟁 테마) 분석 결과를 참고해서 새로 작곡함
   (템포/음계 자기상관·크로마 분석 -- ffmpeg로 wav 변환 후 python으로 추출):
     - 템포 약 92 BPM
     - 크로마 상위: G, A, D, Bb, C / D#(Eb)는 최하위 -- 자연 6도가 살아있는 G 도리안
       (G-A-Bb-C-D-E-F)로 해석. 전쟁/영화음악에서 흔한 "비장한 단조" 색.
     - 곡 전체에 걸쳐 에너지가 꾸준히 상승(4등분 RMS 0.119->0.143) -- 서서히 고조되는 구조
   실제 멜로디를 옮긴 게 아니라 이 특징(템포/조성/고조 곡선)만 참고해서 완전히 새로
   작곡함 -- 저작권 + 콘테스트 원작 규정 때문에 원곡 전사(transcription)는 안 함.
   NES식 4채널(사각파 리드/삼각파 베이스/노이즈 타악기) 구성은 유지. 0은 쉼표. */
/* 전체를 한 옥타브 내려서(2분의 1 주파수) 더 낮고 묵직한 톤으로 -- 멜로디 윤곽은 그대로 */
static const float MUSIC_LEAD_NOTES[MUSIC_STEPS] = {
    /* 마디1 -- 낮고 성긴, 조용히 다가오는 느낌 (G 도리안) */
    98.00f,0,0,0, 116.54f,0,0,0, 130.81f,0,0,0, 98.00f,0,0,0,
    /* 마디2 -- 조금 더 움직임 */
    98.00f,0,116.54f,0, 146.83f,0,130.81f,0, 116.54f,0,98.00f,0, 110.00f,0,0,0,
    /* 마디3 -- 한 옥타브 위로(원래 대비는 유지), 리듬도 촘촘하게 */
    146.83f,0,174.61f,0, 196.00f,0,174.61f,0, 146.83f,0,130.81f,0, 116.54f,0,130.81f,0,
    /* 마디4 -- 절정, 가장 높고 꽉 참(그래도 원래보다 한 옥타브 낮음) */
    196.00f,0,233.08f,0, 293.66f,0,261.63f,0, 233.08f,0,196.00f,0, 174.61f,0,146.83f,0
};
/* 마디별 베이스 근음 -- G 도리안 vamp: i - bVII - i - bIII (G - F - G - Bb) */
static const float MUSIC_BASS_PER_BAR[4] = { 98.00f, 87.31f, 98.00f, 116.54f };
/* 참고 트랙처럼 마디가 갈수록 커지는 다이내믹(고조) -- 전 레이어 진폭에 곱함 */
static const float MUSIC_BAR_VOLUME[4] = { 0.70f, 0.85f, 1.00f, 1.15f };

/* 4분음표(로컬스텝 0,4,8,12)마다 쿵, 그 사이 뒷박(2,6,10,14)마다 탁 -- 마디마다 반복 */
static const int MUSIC_KICK_LOCAL[16]  = { 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0 };
static const int MUSIC_SNARE_LOCAL[16] = { 0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0 };

#define MUSIC_KICK_SAMPLES (MUSIC_SAMPLE_RATE / 12)   /* ~83ms */
#define MUSIC_SNARE_SAMPLES (MUSIC_SAMPLE_RATE / 10)  /* ~100ms */

static HWAVEOUT g_waveOutMusic = NULL;
static WAVEHDR g_musicHdr;
static unsigned char g_musicBuf[MUSIC_TOTAL_SAMPLES];
static BOOL g_musicOutOpen = FALSE;
static BOOL g_musicPlaying = FALSE;

static void GenerateMusicLoop(void) {
    for (int i = 0; i < MUSIC_TOTAL_SAMPLES; i++) {
        int step = i / MUSIC_STEP_SAMPLES;       /* 0..63, 16분음표 단위 */
        int posInStep = i % MUSIC_STEP_SAMPLES;
        int bar = step / 16;                      /* 0..3 */
        int localStep = step % 16;

        float mix = 0.0f;

        /* 리드 멜로디 -- 밝은 사각파. NES 액션게임 특유의 또렷한 리드 채널 느낌 */
        float leadFreq = MUSIC_LEAD_NOTES[step];
        if (leadFreq > 0.0f) {
            float t = (float)posInStep / (float)MUSIC_STEP_SAMPLES;
            float envelope = expf(-t * 3.0f);
            int fadeIn = 20;
            if (posInStep < fadeIn) envelope *= (float)posInStep / (float)fadeIn;
            float phase = fmodf(leadFreq * (float)i / (float)MUSIC_SAMPLE_RATE, 1.0f);
            float square = (phase < 0.5f) ? 1.0f : -1.0f;
            mix += square * envelope * 26.0f;
        }

        /* 베이스 -- 삼각파(NES 베이스 채널 흉내), 4분음표마다 마디의 근음을 퉁김 */
        if (localStep % 4 == 0) {
            float bassFreq = MUSIC_BASS_PER_BAR[bar];
            int posInQuarter = i % MUSIC_QUARTER_SAMPLES;
            float t = (float)posInQuarter / (float)MUSIC_QUARTER_SAMPLES;
            float envelope = expf(-t * 3.0f);
            float phase = fmodf(bassFreq * (float)i / (float)MUSIC_SAMPLE_RATE, 1.0f);
            float tri = 4.0f * fabsf(phase - 0.5f) - 1.0f;
            mix += tri * envelope * 24.0f;
        }

        /* 킥(쿵) -- 피치가 빠르게 떨어지는 사인 버스트, 대포 같은 둔중한 타격감 */
        if (MUSIC_KICK_LOCAL[localStep] && posInStep < MUSIC_KICK_SAMPLES) {
            float kt = (float)posInStep / (float)MUSIC_KICK_SAMPLES;
            float kickFreq = 65.0f - kt * 40.0f; /* 90->40에서 65->25로 더 낮고 묵직하게 */
            float kenv = expf(-kt * 8.0f);
            float ksin = sinf(2.0f * PI_F * kickFreq * (float)posInStep / (float)MUSIC_SAMPLE_RATE);
            mix += ksin * kenv * 50.0f;
        }

        /* 스네어(탁) -- 짧게 끊어지는 노이즈 버스트 */
        if (MUSIC_SNARE_LOCAL[localStep] && posInStep < MUSIC_SNARE_SAMPLES) {
            float st = (float)posInStep / (float)MUSIC_SNARE_SAMPLES;
            float senv = expf(-st * 10.0f);
            int n = (rand() % 256) - 128;
            mix += (float)n * senv * 0.30f;
        }

        mix *= MUSIC_BAR_VOLUME[bar]; /* 참고 트랙처럼 마디가 갈수록 고조되는 다이내믹 */

        int v = 128 + (int)mix;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        g_musicBuf[i] = (unsigned char)v;
    }
}

static void InitMusic(void) {
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = MUSIC_SAMPLE_RATE;
    wfx.wBitsPerSample = 8;
    wfx.nBlockAlign = 1;
    wfx.nAvgBytesPerSec = MUSIC_SAMPLE_RATE;
    if (waveOutOpen(&g_waveOutMusic, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
        g_musicOutOpen = TRUE;
        GenerateMusicLoop();
    }
}

/* 매 프레임 호출 -- 재생 중인 버퍼가 끝났으면(WHDR_DONE) 곧바로 같은 버퍼를 다시 큐에 넣어
   무한 반복시킨다. soundOn이 꺼지거나 로비/옵션이 아니면(게임 시작 후에는 전쟁 앰비언스로
   바뀌므로) 즉시 waveOutReset으로 멈춤. */
static void UpdateMusicLoop(void) {
    if (!g_musicOutOpen) return;
    BOOL shouldPlay = soundOn && (scene == SCENE_LOBBY || scene == SCENE_OPTIONS);
    if (!shouldPlay) {
        if (g_musicPlaying) {
            waveOutReset(g_waveOutMusic);
            g_musicPlaying = FALSE;
        }
        return;
    }
    if (!g_musicPlaying || (g_musicHdr.dwFlags & WHDR_DONE)) {
        if (g_musicHdr.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_waveOutMusic, &g_musicHdr, sizeof(WAVEHDR));
        ZeroMemory(&g_musicHdr, sizeof(WAVEHDR));
        g_musicHdr.lpData = (LPSTR)g_musicBuf;
        g_musicHdr.dwBufferLength = MUSIC_TOTAL_SAMPLES;
        waveOutPrepareHeader(g_waveOutMusic, &g_musicHdr, sizeof(WAVEHDR));
        waveOutWrite(g_waveOutMusic, &g_musicHdr, sizeof(WAVEHDR));
        g_musicPlaying = TRUE;
    }
}

/* ---------- 전쟁 앰비언스: 게임 시작(인트로) 이후 도는 폭발/총성 배경음 ----------
   런타임 랜덤 트리거 방식은 긴장감이 떨어진다는 피드백을 받고, 미리 정해둔(고정)
   타임라인을 하나의 긴 루프 버퍼에 겹쳐 그리는 방식으로 되돌림 -- 대신 이번엔 이벤트를
   훨씬 촘촘하게, 그리고 일부러 서로 겹치게 배치해서(포성 rumble이 깔린 위에 폭발+총성이
   겹침) 빈 공백 없이 입체감 있게 들리도록 설계함. 미션 성공(RESULT_HIT) 순간 멈춘다. */
#define WAR_SAMPLE_RATE 8000
#define WAR_LOOP_SECONDS 16
#define WAR_TOTAL_SAMPLES (WAR_SAMPLE_RATE * WAR_LOOP_SECONDS)

/* 포성 rumble -- 길게 깔리는 초저역. 다른 레이어와 겹쳐도 되게 여유 있게 배치 */
typedef struct { float startSec; float durationSec; } WarRumble;
static const WarRumble WAR_RUMBLES[] = { { 0.0f, 4.0f }, { 8.5f, 4.5f } };
#define WAR_RUMBLE_COUNT (int)(sizeof(WAR_RUMBLES) / sizeof(WAR_RUMBLES[0]))

/* 폭발 -- 낙하 휘파람 + 저음 붐. close면 더 크고 짧게 감쇠 */
typedef struct { float startSec; BOOL close; } WarExplosion;
static const WarExplosion WAR_EXPLOSIONS[] = {
    { 1.0f, FALSE }, { 3.8f, TRUE }, { 6.5f, FALSE }, { 10.0f, TRUE }, { 13.5f, FALSE },
};
#define WAR_EXPLOSION_COUNT (int)(sizeof(WAR_EXPLOSIONS) / sizeof(WAR_EXPLOSIONS[0]))

/* 총성 버스트 -- 폭발 직전/도중에 겹치게 배치한 것도 있음(예: 6.2s가 6.5s 폭발과 겹침) */
typedef struct { float startSec; int shots; float shotGapSec; } WarGunBurst;
static const WarGunBurst WAR_GUN_BURSTS[] = {
    { 0.4f, 2, 0.10f }, { 2.2f, 4, 0.08f }, { 3.5f, 1, 0.0f }, { 5.0f, 5, 0.07f },
    { 6.2f, 3, 0.09f }, { 9.0f, 2, 0.10f }, { 11.0f, 4, 0.08f }, { 12.2f, 1, 0.0f },
    { 14.5f, 3, 0.09f }, { 15.5f, 2, 0.08f },
};
#define WAR_GUN_BURST_COUNT (int)(sizeof(WAR_GUN_BURSTS) / sizeof(WAR_GUN_BURSTS[0]))

static HWAVEOUT g_waveOutWar = NULL;
static WAVEHDR g_warHdr;
static unsigned char g_warBuf[WAR_TOTAL_SAMPLES];
static BOOL g_warOutOpen = FALSE;
static BOOL g_warPlaying = FALSE;

static void GenerateWarLoop(void) {
    static float mixBuf[WAR_TOTAL_SAMPLES];

    /* 배경 브라운노이즈(바람/원거리 소음) -- 아주 옅게 */
    float brown = 0.0f;
    for (int i = 0; i < WAR_TOTAL_SAMPLES; i++) {
        float whiteN = ((float)(rand() % 2001) - 1000.0f) / 1000.0f;
        brown += whiteN * 0.02f;
        if (brown > 1.0f) brown = 1.0f;
        if (brown < -1.0f) brown = -1.0f;
        mixBuf[i] = brown * 5.0f;
    }

    /* 포성 rumble -- 초저역, 서서히 커졌다 작아짐 */
    for (int r = 0; r < WAR_RUMBLE_COUNT; r++) {
        int base = (int)(WAR_RUMBLES[r].startSec * WAR_SAMPLE_RATE);
        int len = (int)(WAR_RUMBLES[r].durationSec * WAR_SAMPLE_RATE);
        float freq = 14.0f + (float)(rand() % 10);
        for (int j = 0; j < len; j++) {
            int idx = base + j;
            if (idx < 0 || idx >= WAR_TOTAL_SAMPLES) continue;
            float t = (float)j / (float)len;
            float env = sinf(PI_F * t) * 0.6f;
            float s = sinf(2.0f * PI_F * freq * (float)j / (float)WAR_SAMPLE_RATE);
            float n = ((float)(rand() % 2001) - 1000.0f) / 1000.0f;
            mixBuf[idx] += (s * 0.85f + n * 0.15f) * env * 30.0f;
        }
    }

    /* 폭발: 낙하 휘파람 + 저음 붐(16~30Hz 위주로 무겁게) */
    for (int e = 0; e < WAR_EXPLOSION_COUNT; e++) {
        BOOL close = WAR_EXPLOSIONS[e].close;
        int base = (int)(WAR_EXPLOSIONS[e].startSec * WAR_SAMPLE_RATE);
        int whistleLen = (int)(0.30f * WAR_SAMPLE_RATE);
        float hiFreq = 700.0f + (float)(rand() % 500);
        for (int j = 0; j < whistleLen; j++) {
            int idx = base + j;
            if (idx < 0 || idx >= WAR_TOTAL_SAMPLES) continue;
            float t = (float)j / (float)whistleLen;
            float freq = hiFreq - t * (hiFreq - 120.0f);
            float env = (1.0f - t) * (close ? 0.55f : 0.30f);
            float s = sinf(2.0f * PI_F * freq * (float)j / (float)WAR_SAMPLE_RATE);
            mixBuf[idx] += s * env * 10.0f;
        }
        int boomStart = base + whistleLen;
        int boomLen = (int)((close ? 1.4f : 1.0f) * WAR_SAMPLE_RATE);
        float subFreq = 16.0f + (float)(rand() % 14);
        for (int j = 0; j < boomLen; j++) {
            int idx = boomStart + j;
            if (idx < 0 || idx >= WAR_TOTAL_SAMPLES) continue;
            float t = (float)j / (float)boomLen;
            float env = expf(-t * (close ? 2.0f : 3.0f));
            float lowFreq = subFreq - t * (subFreq * 0.4f);
            float s = sinf(2.0f * PI_F * lowFreq * (float)j / (float)WAR_SAMPLE_RATE);
            float n = ((float)(rand() % 2001) - 1000.0f) / 1000.0f;
            float amp = close ? 100.0f : 65.0f;
            mixBuf[idx] += (s * 0.78f + n * 0.22f) * env * amp;
        }
    }

    /* 총성: 자동사격처럼 여러 발 연달아, 저음(55~95Hz) 섞어서 가볍지 않게 */
    for (int b = 0; b < WAR_GUN_BURST_COUNT; b++) {
        float shotFreq = 55.0f + (float)(rand() % 40);
        for (int s = 0; s < WAR_GUN_BURSTS[b].shots; s++) {
            int base = (int)((WAR_GUN_BURSTS[b].startSec + (float)s * WAR_GUN_BURSTS[b].shotGapSec) * WAR_SAMPLE_RATE);
            int shotLen = (int)(0.03f * WAR_SAMPLE_RATE);
            for (int j = 0; j < shotLen; j++) {
                int idx = base + j;
                if (idx < 0 || idx >= WAR_TOTAL_SAMPLES) continue;
                float t = (float)j / (float)shotLen;
                float env = expf(-t * 14.0f);
                float n = ((float)(rand() % 2001) - 1000.0f) / 1000.0f;
                float low = sinf(2.0f * PI_F * shotFreq * (float)j / (float)WAR_SAMPLE_RATE);
                mixBuf[idx] += (n * 0.45f + low * 0.55f) * env * 60.0f;
            }
        }
    }

    for (int i = 0; i < WAR_TOTAL_SAMPLES; i++) {
        int v = 128 + (int)mixBuf[i];
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        g_warBuf[i] = (unsigned char)v;
    }
}

static void InitWar(void) {
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = WAR_SAMPLE_RATE;
    wfx.wBitsPerSample = 8;
    wfx.nBlockAlign = 1;
    wfx.nAvgBytesPerSec = WAR_SAMPLE_RATE;
    if (waveOutOpen(&g_waveOutWar, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
        g_warOutOpen = TRUE;
        GenerateWarLoop();
    }
}

/* 인트로/게임플레이 중에만, 그리고 미션 성공(RESULT_HIT) 전까지만 재생 */
static void UpdateWarLoop(void) {
    if (!g_warOutOpen) return;
    BOOL inMission = (scene == SCENE_INTRO || scene == SCENE_GAMEPLAY);
    BOOL shouldPlay = soundOn && inMission && finalResult != RESULT_HIT;
    if (!shouldPlay) {
        if (g_warPlaying) {
            waveOutReset(g_waveOutWar);
            g_warPlaying = FALSE;
        }
        return;
    }
    if (!g_warPlaying || (g_warHdr.dwFlags & WHDR_DONE)) {
        if (g_warHdr.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_waveOutWar, &g_warHdr, sizeof(WAVEHDR));
        ZeroMemory(&g_warHdr, sizeof(WAVEHDR));
        g_warHdr.lpData = (LPSTR)g_warBuf;
        g_warHdr.dwBufferLength = WAR_TOTAL_SAMPLES;
        waveOutPrepareHeader(g_waveOutWar, &g_warHdr, sizeof(WAVEHDR));
        waveOutWrite(g_waveOutWar, &g_warHdr, sizeof(WAVEHDR));
        g_warPlaying = TRUE;
    }
}

/* ---------- 버튼 ---------- */

typedef struct { int x, y, w, h; uint32_t color; const wchar_t *label; } Button;

static BOOL PointInRect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}
static BOOL ButtonClicked(Button *b) {
    BOOL hover = !inputBlocked && PointInRect(mouseX, mouseY, b->x, b->y, b->w, b->h);
    uint32_t c = b->color;
    if (hover) {
        int r = (int)((c >> 16) & 0xFF) + 30; if (r > 255) r = 255;
        int g = (int)((c >> 8) & 0xFF) + 30;  if (g > 255) g = 255;
        int bl = (int)(c & 0xFF) + 30;        if (bl > 255) bl = 255;
        c = 0xFF000000 | (r << 16) | (g << 8) | bl;
        cursorHot = TRUE;
    }
    FillPixelRect(b->x, b->y, b->w, b->h, c);
    DrawRectOutline(b->x, b->y, b->w, b->h, 0xFFEEEEEE);
    /* 텍스트는 픽셀과 별도 레이어라 딤 처리로는 안 가려짐 -- 차단된 상태면 아예 큐에 안 넣는다
       (안 그러면 옵션 팝업 위로 뒤쪽 로비 버튼 글자가 그대로 비쳐 보임) */
    if (!inputBlocked) QueueTextCentered(b->x, b->y, b->w, b->h, 1, 0xFFFFFFFF, b->label);
    return hover && mouseDown && !mouseDownPrev;
}

/* ---------- 씬: 로비 ---------- */

static void RenderLobby(void) {
    if (lobbyBgLoaded) {
        memcpy(framebuffer, lobbyBgPixels, sizeof(framebuffer));
    } else {
        ClearScreen(0xFF1B3A3A); /* bg placeholder */
    }

    /* 로고: 로비 진입 시 페이드인 + 살짝 아래에서 위로 올라오는 애니메이션 */
    float logoProgress = lobbyLogoTimer / LOGO_ANIM_TIME;
    if (logoProgress > 1.0f) logoProgress = 1.0f;
    if (logoLoaded) {
        int animY = LOGO_Y + (int)(LOGO_RISE_PX * (1.0f - logoProgress) + 0.5f);
        BlitSpriteAlpha(logoPixels, logoW, logoH, LOGO_X, animY, LOGO_CHROMA_KEY, logoProgress);
    } else if (lobbyBgLoaded) {
        /* 사진 배경 위라 제목 글자가 묻히지 않게 반투명 검정 배경을 살짝 깔아줌 */
        FillPixelRectAlpha(170, 40, 300, 90, 0x000000, 0.45f);
        if (!inputBlocked) QueueTextCentered(170, 40, 300, 90, 1, 0xFFFFFFFF, T("게임 타이틀", "GAME TITLE"));
    } else {
        FillPixelRect(170, 40, 300, 90, 0xFF6B3FA0); /* title image placeholder */
        DrawRectOutline(170, 40, 300, 90, 0xFFEEEEEE);
        if (!inputBlocked) QueueTextCentered(170, 40, 300, 90, 1, 0xFFFFFFFF, T("게임 타이틀", "GAME TITLE"));
    }

    /* 옵션 -- 버튼 대신 우상단 톱니바퀴 아이콘 */
    int gearCx = GAME_W - 30, gearCy = 30, gearR = 14;
    int gdx = mouseX - gearCx, gdy = mouseY - gearCy;
    BOOL gearHover = !inputBlocked && (gdx * gdx + gdy * gdy <= gearR * gearR);
    if (gearHover) cursorHot = TRUE;
    DrawGearIcon(gearCx, gearCy, gearR, gearHover ? 0xFFEEDD66 : 0xFFAAAAAA);
    if (gearHover && mouseDown && !mouseDownPrev) {
        sceneBeforeOptions = SCENE_LOBBY;
        scene = SCENE_OPTIONS;
    }

    /* 시작 -- 버튼 대신 "아무키나 눌러서 시작" 문구, 세로 90% 지점(화면 하단 쪽).
       ESC는 이제 게임 종료(아래 UpdateGame)라서 "아무키"에서 제외해야 충돌 안 남.
       마우스 클릭으로도 시작되지만, 톱니바퀴를 누른 클릭은 옵션만 열고 시작은 안 되게 함 */
    if (!inputBlocked) {
        QueueTextCentered(0, GAME_H * 9 / 10 - 12, GAME_W, 24, 1, 0xFFFFFFFF, T("아무키나 눌러서 시작", "Press Any Key to Start"));
        BOOL startTriggered = FALSE;
        for (int i = 0; i < 256; i++) {
            if (i == VK_ESCAPE) continue;
            if (keys[i] && !keysPrev[i]) { startTriggered = TRUE; break; }
        }
        if (!gearHover && mouseDown && !mouseDownPrev) startTriggered = TRUE;
        if (startTriggered) {
            scene = SCENE_INTRO;
            introTimer = 0.0f;
        }
    }
}

/* ---------- 씬: 옵션 오버레이 ---------- */

static void RenderOptions(void) {
    FillPixelRect(140, 90, 360, 180, 0xFF2A2A3A);
    DrawRectOutline(140, 90, 360, 180, 0xFFEEEEEE);
    QueueTextPoint(160, 110, 1, 0xFFFFFFFF, T("옵션", "Options"));

    char soundLabelU8[32];
    if (langIndex == 1) wsprintfA(soundLabelU8, "Sound: %s", soundOn ? "On" : "Off");
    else wsprintfA(soundLabelU8, "사운드: %s", soundOn ? "켬" : "끔");
    Button soundBtn = { 170, 150, 300, 40, 0xFFD08020, Kor(soundLabelU8) };
    if (ButtonClicked(&soundBtn)) soundOn = !soundOn;

    char langLabelU8[32];
    if (langIndex == 1) wsprintfA(langLabelU8, "Language: %s", LANG_NAMES_EN[langIndex]);
    else wsprintfA(langLabelU8, "언어: %s", LANG_NAMES[langIndex]);
    Button langBtn = { 170, 200, 300, 40, 0xFF20A0A0, Kor(langLabelU8) };
    if (ButtonClicked(&langBtn)) langIndex = (langIndex + 1) % 3;

    Button backBtn = { 170, 250, 300, 40, 0xFF606060, T("뒤로", "Back") };
    if (ButtonClicked(&backBtn) || (keys[VK_ESCAPE] && !keysPrev[VK_ESCAPE])) {
        scene = sceneBeforeOptions;
    }
}

/* ---------- 씬: 인트로 ---------- */

static void RenderIntro(void) {
    ClearScreen(0xFF241B45); /* bg placeholder */

    FillPixelRect(40, 230, GAME_W - 80, 100, 0xFF4A3524); /* npc dialogue box */
    DrawRectOutline(40, 230, GAME_W - 80, 100, 0xFFEEEEEE);
    QueueTextPoint(60, 248, 0, 0xFFFFE0B0, T("치지직.. 대대에서 유일한 생존자는", "Krzzt.. sole survivor of the battalion,"));
    QueueTextPoint(60, 274, 0, 0xFFFFE0B0, T("마지막 명령을 수행하길 바란다.", "carry out the final order."));
    QueueTextPoint(60, 300, 0, 0xFFFFE0B0, T("지금부터 사격재원을 불러주겠다.", "Fire data will be called out now."));

    /* 클릭/스페이스로 계속하라는 뜻으로 말풍선 우측 하단에서 위아래로 왔다갔다하는 화살표 */
    float bounce = sinf(introTimer * 5.0f) * 4.0f;
    DrawArrowDown(GAME_W - 60, 310 + (int)bounce, 7, 0xFFFFE0B0);

    cursorHot = TRUE; /* 인트로는 화면 아무데나 클릭해도 스킵되므로 전체가 클릭 가능 지점 */

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
    bubbleIntroActive = TRUE;
    bubbleIntroPhase = 0;
    bubbleIntroTimer = 0.0f;
    PlayStaticNoise();

    missionFailed = FALSE;
    failTimer = 0.0f;

    azErrorRecorded = 0.0f;
    elErrorRecorded = 0.0f;
    finalResult = RESULT_NONE;
    resultTimer = 0.0f;

    fireHits = 0;
    fireBulletPos = 0.0f;
    fireBulletDir = 1.0f;
    fireBulletSpeed = FIRE_BULLET_SPEED_BASE;
    fireMarkPos = 0.5f;
    fireStageFailed = FALSE;
    fireSquareX = (float)FIRE_SQUARE_START_X;

    chargeDragIdx = -1;

    draggingLever = FALSE;
}

/* 원반 i가 통 안에 "완전히" 들어와 있는지 -- 중심점이 아니라 원 전체(반지름까지)가
   통 사각형 안쪽이어야 함. 살짝 걸치기만 해도 합산에서 빠진다. */
static BOOL ChargeDiskFullyInBin(int i) {
    return chargeX[i] - CHARGE_RADIUS >= (float)CHARGE_BIN_X &&
           chargeX[i] + CHARGE_RADIUS <= (float)(CHARGE_BIN_X + CHARGE_BIN_W) &&
           chargeY[i] - CHARGE_RADIUS >= (float)CHARGE_BIN_Y &&
           chargeY[i] + CHARGE_RADIUS <= (float)(CHARGE_BIN_Y + CHARGE_BIN_H);
}

/* 3단계(사격) 통과 후 4단계 진입 시 무더기를 새로 쌓는다. 목표가 10~18이라
   원반 하나(1~5) 값으로는 절대 못 맞추고 항상 여러 개를 더해야 한다. 풀이가
   반드시 존재하도록, 목표를 정확히 채우는 원반 조합을 먼저 만들어서 앞쪽
   인덱스에 심어두고 나머지는 전부 랜덤(미끼)으로 채운다. */
static void EnterChargeStage(void) {
    chargeTargetNumber = CHARGE_TARGET_MIN + rand() % (CHARGE_TARGET_MAX - CHARGE_TARGET_MIN + 1);
    int remaining = chargeTargetNumber;
    int solutionCount = 0;
    while (remaining > 0 && solutionCount < CHARGE_COUNT - 1) {
        int v = CHARGE_DISK_MIN + rand() % (CHARGE_DISK_MAX - CHARGE_DISK_MIN + 1);
        if (v > remaining) v = remaining;
        chargeNumber[solutionCount] = v;
        remaining -= v;
        solutionCount++;
    }
    for (int i = 0; i < CHARGE_COUNT; i++) {
        if (i >= solutionCount) chargeNumber[i] = CHARGE_DISK_MIN + rand() % (CHARGE_DISK_MAX - CHARGE_DISK_MIN + 1);
        chargeX[i] = (float)(CHARGE_PILE_CX - CHARGE_PILE_SPREAD_X + rand() % (2 * CHARGE_PILE_SPREAD_X + 1));
        chargeY[i] = (float)(CHARGE_PILE_CY - CHARGE_PILE_SPREAD_Y + rand() % (2 * CHARGE_PILE_SPREAD_Y + 1));
        chargeZ[i] = i;
    }
    chargeNextZ = CHARGE_COUNT;
    chargeDragIdx = -1;
}

static void UpdateIntro(float dt) {
    (void)dt;
    if ((keys[VK_SPACE] && !keysPrev[VK_SPACE]) || (mouseDown && !mouseDownPrev)) { EnterGameplay(); return; }
}

/* ---------- 씬: 조작화면 ---------- */

/* 말풍선이 새 phase로 넘어갈 때(=화면에 새 숫자가 뜨는 순간) 그 자리 숫자를 음성으로 콜아웃.
   방위각/사각은 phase가 짝수(0,2,4,6)일 때마다 그 자리 숫자 하나씩, 장약은 문장이 다시
   나타나는 사이클 시작(phase==0)에 두 자리 숫자를 이어서 읽는다. */
static void TriggerBubbleVoice(int phase) {
    if (fireStage == FIRESTAGE_CHARGE) {
        if (phase == 0) PlayVoiceNumber(chargeTargetNumber, 2);
        return;
    }
    if (phase % 2 != 0) return; /* 홀수=공백 구간 */
    int bubbleTarget = (fireStage == FIRESTAGE_AZIMUTH) ? azTargetMil : elTargetMil;
    int digits[4] = {
        (bubbleTarget / 1000) % 10, (bubbleTarget / 100) % 10,
        (bubbleTarget / 10) % 10, bubbleTarget % 10
    };
    PlayVoiceDigit(digits[phase / 2]);
}

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

    /* 말풍선: 방위각/사각 두 스테이지에서만 사용. 스테이지 진입 직후엔 핑크 배경만 빠르게
       3번 깜빡이는 "치직" 연출이 먼저 재생되고, 그게 끝난 뒤에야 자릿수 콜아웃이 시작된다. */
    if (fireStage != FIRESTAGE_FIRE) {
        if (bubbleIntroActive) {
            bubbleIntroTimer += dt;
            if (bubbleIntroTimer >= BUBBLE_INTRO_FLASH_TIME) {
                bubbleIntroTimer = 0.0f;
                bubbleIntroPhase++;
                if (bubbleIntroPhase >= BUBBLE_INTRO_FLASH_COUNT) {
                    bubbleIntroActive = FALSE;
                    bubblePhase = 0;
                    bubbleTimer = 0.0f;
                    TriggerBubbleVoice(0);
                }
            }
        } else {
            bubbleTimer += dt;
            BOOL isChargeStage = (fireStage == FIRESTAGE_CHARGE);
            const float *phaseDur = isChargeStage ? BUBBLE_PHASE_DUR_CHARGE : BUBBLE_PHASE_DUR;
            int phaseCount = isChargeStage ? CHARGE_BUBBLE_PHASE_COUNT : 8;
            if (bubbleTimer >= phaseDur[bubblePhase]) {
                bubbleTimer = 0.0f;
                bubblePhase = (bubblePhase + 1) % phaseCount;
                TriggerBubbleVoice(bubblePhase);
            }
        }
    }

    if (leverFlash > 0.0f) leverFlash -= dt;

    /* 스페이스로 확정 -> 페이드아웃 -> 다음 스테이지로 페이드인 (마지막 스테이지면 최종판정) */
    if (transState == TRANS_OUT) {
        transTimer += dt;
        if (transTimer >= TRANS_TIME) {
            if (fireStage == FIRESTAGE_AZIMUTH) {
                fireStage = FIRESTAGE_ELEVATION;
                bubblePhase = 0;
                bubbleTimer = 0.0f;
                bubbleIntroActive = TRUE;
                bubbleIntroPhase = 0;
                bubbleIntroTimer = 0.0f;
                PlayStaticNoise();
                transState = TRANS_IN;
                transTimer = 0.0f;
            } else if (fireStage == FIRESTAGE_ELEVATION) {
                fireStage = FIRESTAGE_FIRE;
                fireHits = 0;
                fireBulletPos = 0.0f;
                fireBulletDir = 1.0f;
                fireBulletSpeed = FIRE_BULLET_SPEED_BASE;
                fireMarkPos = 0.08f + (float)(rand() % 840) / 1000.0f;
                fireStageFailed = FALSE;
                fireSquareX = (float)FIRE_SQUARE_START_X;
                transState = TRANS_IN;
                transTimer = 0.0f;
            } else if (fireStage == FIRESTAGE_FIRE) {
                if (fireStageFailed) {
                    /* 타이밍 실패는 여기서 바로 게임오버 -- 4단계로 넘어가지 않음 */
                    finalResult = RESULT_FIRE_FAIL;
                } else {
                    fireStage = FIRESTAGE_CHARGE;
                    EnterChargeStage();
                    bubblePhase = 0;
                    bubbleTimer = 0.0f;
                    bubbleIntroActive = TRUE;
                    bubbleIntroPhase = 0;
                    bubbleIntroTimer = 0.0f;
                    PlayStaticNoise();
                }
                transState = TRANS_IN;
                transTimer = 0.0f;
            } else {
                /* FIRESTAGE_CHARGE 완료 -- 여기 도달했다는 건 항상 올바른 장약을 제출했다는 뜻 */
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

    if (fireStage == FIRESTAGE_FIRE) {
        /* 3단계: 게이지 위를 오가는 불릿을 붉은 표시와 맞춰 좌클릭으로 저격 */
        fireBulletPos += fireBulletDir * (fireBulletSpeed / (float)FIRE_GAUGE_W) * dt;
        if (fireBulletPos >= 1.0f) { fireBulletPos = 1.0f; fireBulletDir = -1.0f; }
        if (fireBulletPos <= 0.0f) { fireBulletPos = 0.0f; fireBulletDir = 1.0f; }

        float target = (float)FIRE_SQUARE_START_X - (float)fireHits *
                       ((float)(FIRE_SQUARE_START_X - FIRE_SQUARE_MIN_X) / (float)FIRE_HITS_REQUIRED);
        fireSquareX += (target - fireSquareX) * dt * 8.0f;

        if (mouseDown && !mouseDownPrev) {
            float bulletPx = fireBulletPos * (float)FIRE_GAUGE_W;
            float markPx = fireMarkPos * (float)FIRE_GAUGE_W;
            if (fabsf(bulletPx - markPx) <= FIRE_MARK_TOLERANCE_PX) {
                fireHits++;
                fireBulletSpeed += FIRE_BULLET_SPEED_STEP;
                leverFlash = LEVER_FLASH_TIME;
                PlayHitSound();
                fireMarkPos = 0.08f + (float)(rand() % 840) / 1000.0f;
                if (fireHits >= FIRE_HITS_REQUIRED) {
                    fireStageFailed = FALSE;
                    transState = TRANS_OUT;
                    transTimer = 0.0f;
                }
            } else {
                fireStageFailed = TRUE;
                transState = TRANS_OUT;
                transTimer = 0.0f;
            }
        }
        return;
    }

    if (fireStage == FIRESTAGE_CHARGE) {
        /* 4단계: 무더기에서 목표 번호 원반을 찾아 드래그로 통에 넣기 */
        BOOL anyHover = FALSE;
        for (int i = 0; i < CHARGE_COUNT; i++) {
            float hdx = (float)mouseX - chargeX[i], hdy = (float)mouseY - chargeY[i];
            if (hdx * hdx + hdy * hdy <= (float)(CHARGE_RADIUS * CHARGE_RADIUS)) { anyHover = TRUE; break; }
        }
        if (anyHover || chargeDragIdx >= 0) cursorHot = TRUE;

        if (mouseDown && !mouseDownPrev && chargeDragIdx < 0) {
            /* 겹쳐 쌓여있으니 z가 가장 큰(맨 위) 원반부터 집는다 */
            int best = -1, bestZ = -1;
            for (int i = 0; i < CHARGE_COUNT; i++) {
                float hdx = (float)mouseX - chargeX[i], hdy = (float)mouseY - chargeY[i];
                if (hdx * hdx + hdy * hdy <= (float)(CHARGE_RADIUS * CHARGE_RADIUS) && chargeZ[i] > bestZ) {
                    best = i; bestZ = chargeZ[i];
                }
            }
            if (best >= 0) {
                chargeDragIdx = best;
                chargeZ[best] = chargeNextZ++; /* 집으면 맨 위로 올라옴 */
            }
        }
        if (chargeDragIdx >= 0) {
            if (mouseDown) {
                chargeX[chargeDragIdx] = (float)mouseX;
                chargeY[chargeDragIdx] = (float)mouseY;
            } else {
                chargeDragIdx = -1;
                /* 정답은 "정확히 하나"가 아니라 통 안에 있는 원반들의 합 -- 3+4도, 1을 7개도 됨.
                   그래서 특정 원반이 아니라 매번 놓을 때마다 통 안 전체를 다시 합산해서 검사.
                   (통에서 다시 꺼내면 자동으로 빠짐 -- 별도의 "제출됨" 상태가 없어도 됨) */
                int sum = 0;
                for (int i = 0; i < CHARGE_COUNT; i++) {
                    if (ChargeDiskFullyInBin(i)) sum += chargeNumber[i];
                }
                if (sum == chargeTargetNumber) {
                    transState = TRANS_OUT;
                    transTimer = 0.0f;
                }
            }
        }
        return;
    }

    int dx = mouseX - leverCx, dy = mouseY - leverCy;
    float distToPivot = sqrtf((float)(dx * dx + dy * dy));

    if (fireStage == FIRESTAGE_AZIMUTH) {
        if (distToPivot <= leverPivotGrabR) cursorHot = TRUE;

        /* 좌클릭 드래그: 기어비 적용 -- 전체 범위(6400밀)를 커버하려면 최대 15바퀴 돌려야 함 */
        if (mouseDown && !mouseDownPrev && distToPivot <= leverPivotGrabR) {
            draggingLever = TRUE;
            lastDragAngle = atan2f((float)dy, (float)dx) * 180.0f / PI_F;
            azLastTickMil = (int)NormalizeMil(azAngleDeg * AZ_MIL_PER_DEG) / TICK_MIL_STEP;
        }
        if (!mouseDown) draggingLever = FALSE;
        if (draggingLever) {
            float curAngle = atan2f((float)dy, (float)dx) * 180.0f / PI_F;
            float delta = curAngle - lastDragAngle;
            while (delta > 180.0f) delta -= 360.0f;
            while (delta < -180.0f) delta += 360.0f;
            azAngleDeg += delta;
            lastDragAngle = curAngle;
            /* 10밀 단위로 넘어갈 때마다 짧게 딸깍 -- 빠르게 돌리면 "띠리릭" 연속음처럼 들림 */
            int curTickMil = (int)NormalizeMil(azAngleDeg * AZ_MIL_PER_DEG) / TICK_MIL_STEP;
            if (curTickMil != azLastTickMil) {
                PlayTick();
                azLastTickMil = curTickMil;
            }
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
        if (inElevationGrabZone) cursorHot = TRUE;

        if (mouseDown && !mouseDownPrev && inElevationGrabZone) {
            draggingLever = TRUE;
            lastDragMouseY = mouseY;
            elVelocity = 0.0f;
            elLastTickMil = (int)(elCurrentMil + 0.5f) / TICK_MIL_STEP;
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
            int curTickMil = (int)(elCurrentMil + 0.5f) / TICK_MIL_STEP;
            if (curTickMil != elLastTickMil) {
                PlayTick();
                elLastTickMil = curTickMil;
            }
        } else if (fabsf(elVelocity) > EL_VELOCITY_STOP_MIL) {
            /* 손 뗀 뒤 관성으로 계속 미끄러지다가 마찰로 서서히 멈춤 (휴대폰 슬라이드 느낌) */
            elCurrentMil += elVelocity * dt;
            float decay = 1.0f - EL_FRICTION * dt;
            if (decay < 0.0f) decay = 0.0f;
            elVelocity *= decay;
            if (elCurrentMil < EL_ADJUST_MIN) { elCurrentMil = EL_ADJUST_MIN; elVelocity = 0.0f; }
            if (elCurrentMil > EL_ADJUST_MAX) { elCurrentMil = EL_ADJUST_MAX; elVelocity = 0.0f; }
            int curTickMil = (int)(elCurrentMil + 0.5f) / TICK_MIL_STEP;
            if (curTickMil != elLastTickMil) {
                PlayTick();
                elLastTickMil = curTickMil;
            }
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

        /* 스페이스: 지금 값을 확정 -- 확정되면 3단계(사격)로 넘어감 */
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
        QueueTextCentered(0, 0, GAME_W, GAME_H, 2, 0xFFFF3030, T("포반 전멸", "SECTION WIPED OUT"));
        return;
    }

    if (finalResult != RESULT_NONE) {
        ClearScreen(0xFF000000);
        const wchar_t *msg;
        uint32_t col;
        if (finalResult == RESULT_HIT) { msg = T("명중", "HIT"); col = 0xFF3CFF6E; }
        else if (finalResult == RESULT_MISS) { msg = T("임무 실패", "MISSION FAILED"); col = 0xFFFF3030; }
        else { msg = T("사격 실패", "FIRE FAILED"); col = 0xFFFF3030; }
        QueueTextCentered(0, 0, GAME_W, GAME_H, 2, col, msg);
    } else {
        ClearScreen(0xFF161B12); /* bg placeholder */

        /* 좌측 숫자 말풍선 -- 방위각/사각/장약 스테이지에서만. 3단계(사격)에는 없음.
           장약은 문장이라("장약 N 키로!!") 다른 스테이지의 밀 콜아웃 박스보다 넓게 씀 */
        BOOL isCharge = (fireStage == FIRESTAGE_CHARGE);
        int bubbleX = isCharge ? CHARGE_BUBBLE_X : 30;
        int bubbleY = isCharge ? CHARGE_BUBBLE_Y : 130;
        int bubbleW = isCharge ? CHARGE_BUBBLE_W : 110;
        int bubbleH = isCharge ? CHARGE_BUBBLE_H : 100;
        if (fireStage != FIRESTAGE_FIRE) {
            if (bubbleIntroActive) {
                BOOL flashOn = (bubbleIntroPhase % 2 == 0);
                if (flashOn) {
                    FillPixelRect(bubbleX, bubbleY, bubbleW, bubbleH, 0xFF6A2540);
                    DrawRectOutline(bubbleX, bubbleY, bubbleW, bubbleH, 0xFFEEEEEE);
                }
            } else if (bubblePhase % 2 == 0) {
                FillPixelRect(bubbleX, bubbleY, bubbleW, bubbleH, 0xFF6A2540);
                DrawRectOutline(bubbleX, bubbleY, bubbleW, bubbleH, 0xFFEEEEEE);
                if (isCharge) {
                    /* 문장 전체를 계속 같은 자리에 표시(자릿수 순차 콜아웃 불필요) */
                    char chargeCallU8[32];
                    if (langIndex == 1) wsprintfA(chargeCallU8, "Charge %d, Load!!", chargeTargetNumber);
                    else wsprintfA(chargeCallU8, "장약 %d 키로!!", chargeTargetNumber);
                    QueueTextCentered(bubbleX, bubbleY, bubbleW, bubbleH, 1, 0xFFFFFFFF, Kor(chargeCallU8));
                } else {
                    int bubbleTarget = (fireStage == FIRESTAGE_AZIMUTH) ? azTargetMil : elTargetMil;
                    int digits[4] = {
                        (bubbleTarget / 1000) % 10, (bubbleTarget / 100) % 10,
                        (bubbleTarget / 10) % 10, bubbleTarget % 10
                    };
                    DrawNumber(30 + 40, 130 + 40, digits[bubblePhase / 2], 1, 0xFFFFFFFF);
                }
            }
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

        /* 중앙 숫자판 -- 3단계(사격)/4단계(장약)에는 그 자리에 다른 오브젝트가 있어서 생략 */
        if (fireStage == FIRESTAGE_AZIMUTH || fireStage == FIRESTAGE_ELEVATION) {
            FillPixelRect(230, 140, 160, 100, 0xFF1E2A4A);
            DrawRectOutline(230, 140, 160, 100, 0xFFEEEEEE);
        }
        if (fireStage == FIRESTAGE_AZIMUTH) {
            QueueTextCentered(230, 146, 160, 16, 0, 0xFF88AACC, T("방위각 (밀)", "Azimuth (mil)"));
            int curMil = (int)NormalizeMil(azAngleDeg * AZ_MIL_PER_DEG);
            DrawNumber(255, 190, curMil, 4, 0xFF3CFF6E);
        } else if (fireStage == FIRESTAGE_ELEVATION) {
            QueueTextCentered(230, 146, 160, 16, 0, 0xFF88AACC, T("사각 (밀)", "Elevation (mil)"));
            DrawNumber(255, 190, (int)(elCurrentMil + 0.5f), 4, 0xFF3CFF6E);
        }

        /* 우측 조작부 -- 방위각: 회전 다이얼 / 사각: 세로 릴 / 사격: 타이밍 게이지 */
        if (fireStage == FIRESTAGE_AZIMUTH) {
            QueueTextCentered(leverCx - 90, leverCy - leverPivotGrabR - 26, 180, 20, 0, 0xFFEEEEEE, T("잡고 돌리시오", "Grip and Turn"));
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
        } else if (fireStage == FIRESTAGE_ELEVATION) {
            QueueTextCentered(leverCx - 90, leverCy - EL_GRAB_HALF_H - 26, 180, 20, 0, 0xFFEEEEEE, T("밀어서 조정하시오", "Push to Adjust"));
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
        } else if (fireStage == FIRESTAGE_FIRE) {
            /* 3단계: 좌측 사각 오브젝트 + 우측 타이밍 게이지 (붉은 표시 위를 불릿이 왕복) */
            FillPixelRect((int)fireSquareX, FIRE_SQUARE_Y, FIRE_SQUARE_SIZE, FIRE_SQUARE_SIZE, 0xFF3CFF6E);
            DrawRectOutline((int)fireSquareX, FIRE_SQUARE_Y, FIRE_SQUARE_SIZE, FIRE_SQUARE_SIZE, 0xFFEEEEEE);

            /* 우측 게이지 위 안내문구 -- 방위각/사각 스테이지의 "잡고 돌리시오"류와 같은 자리 */
            QueueTextCentered(FIRE_GAUGE_X - 25, FIRE_GAUGE_Y - 26, FIRE_GAUGE_W + 50, 20, 0,
                               0xFFEEEEEE, T("좌클릭으로 발사", "Left-Click to Fire"));
            DrawRectOutline(FIRE_GAUGE_X, FIRE_GAUGE_Y, FIRE_GAUGE_W, FIRE_GAUGE_H, 0xFFAAAAAA);
            /* 붉은 표시 폭 = 실제 명중 판정 허용폭(FIRE_MARK_TOLERANCE_PX)의 2배 -- 눈에 보이는
               빨간 구간이 곧 정확한 히트 존이 되도록, 그리고 불릿보다 확실히 길게 */
            int markX = FIRE_GAUGE_X + (int)(fireMarkPos * (float)FIRE_GAUGE_W);
            int markHalfW = (int)FIRE_MARK_TOLERANCE_PX;
            FillPixelRect(markX - markHalfW, FIRE_GAUGE_Y, markHalfW * 2, FIRE_GAUGE_H, 0xFFFF3030);
            int bulletX = FIRE_GAUGE_X + (int)(fireBulletPos * (float)FIRE_GAUGE_W);
            FillPixelRect(bulletX - 3, FIRE_GAUGE_Y - 8, 6, FIRE_GAUGE_H + 16,
                          (leverFlash > 0.0f) ? 0xFFFFB020 : 0xFFFFD020);
        } else {
            /* 4단계: 번호 원반 무더기(드래그로 치우며 탐색) + 통(제출함, 위아래로 화살표가 왔다갔다) */
            DrawRectOutline(CHARGE_BIN_X, CHARGE_BIN_Y, CHARGE_BIN_W, CHARGE_BIN_H, 0xFFAAAAAA);
            float binBounce = sinf(gpElapsed * 4.0f) * 10.0f;
            DrawArrowDown(CHARGE_BIN_X + CHARGE_BIN_W / 2, CHARGE_BIN_Y - 6 + (int)binBounce, 7, 0xFFFFD020);

            /* z 순서(작은 값부터)로 그려서 나중 그려지는(z가 큰) 원반이 위로 보이게 */
            int binSum = 0;
            for (int z = 0; z < chargeNextZ; z++) {
                for (int i = 0; i < CHARGE_COUNT; i++) {
                    if (chargeZ[i] != z) continue;
                    uint32_t col = (i == chargeDragIdx) ? 0xFFE0C060 : 0xFFC9A227;
                    FillCircle((int)chargeX[i], (int)chargeY[i], CHARGE_RADIUS, col);
                    DrawRing((int)chargeX[i], (int)chargeY[i], (float)CHARGE_RADIUS, 0xFF6B5615);
                    DrawNumber((int)chargeX[i] - 4, (int)chargeY[i] - 9, chargeNumber[i], 1, 0xFF2A2200);
                    if (ChargeDiskFullyInBin(i)) binSum += chargeNumber[i];
                }
            }
            /* 통 안 원반들의 현재 합계 -- 몇 개를 넣었는지, 얼마나 더 필요한지 감으로 알 수 있게 */
            char binSumU8[16];
            if (langIndex == 1) wsprintfA(binSumU8, "Sum %d", binSum);
            else wsprintfA(binSumU8, "합 %d", binSum);
            QueueTextCentered(CHARGE_BIN_X - 20, CHARGE_BIN_Y + CHARGE_BIN_H + 6, CHARGE_BIN_W + 40, 16,
                               0, 0xFFAAAAAA, Kor(binSumU8));
        }

        /* 하단 단축키 안내 */
        if (fireStage == FIRESTAGE_AZIMUTH || fireStage == FIRESTAGE_ELEVATION) {
            QueueTextPoint(20, GAME_H - 32, 0, 0xFFAAAAAA, T("드래그: 큰 폭 조정", "Drag: Coarse Adjust"));
            QueueTextPoint(20, GAME_H - 20, 0, 0xFFAAAAAA, T("우클릭: 미세 조정", "Right-Click: Fine Adjust"));
            QueueTextPoint(200, GAME_H - 20, 0, 0xFFAAAAAA, T("스페이스: 확정", "Space: Confirm"));
        } else if (fireStage == FIRESTAGE_FIRE) {
            QueueTextPoint(20, GAME_H - 20, 0, 0xFFAAAAAA, T("표시와 겹칠 때 좌클릭으로 발사", "Left-Click when aligned with mark"));
        } else {
            QueueTextPoint(20, GAME_H - 20, 0, 0xFFAAAAAA, T("드래그로 치우고 합이 맞게 통에 넣으시오", "Drag disks aside, drop matching sum in bin"));
        }
        QueueTextPoint(GAME_W - 130, GAME_H - 20, 0, 0xFFAAAAAA, T("ESC: 나가기", "ESC: Exit"));
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
    cursorHot = FALSE;
    if (scene == SCENE_INTRO) cursorHot = TRUE; /* 인트로는 화면 전체가 스킵 클릭 가능 지점 */

    /* 로고 페이드인 타이머: 로비(또는 옵션)에 막 들어왔을 때만 0부터 다시 재생 */
    BOOL lobbyIsh = (scene == SCENE_LOBBY || scene == SCENE_OPTIONS);
    if (lobbyIsh) {
        if (!lobbyWasActive) lobbyLogoTimer = 0.0f;
        else lobbyLogoTimer += dt;
        lobbyWasActive = TRUE;
    } else {
        lobbyWasActive = FALSE;
    }

    switch (scene) {
        case SCENE_LOBBY:
            /* 종료 버튼을 없앤 대신 ESC로 게임을 닫음 */
            if (keys[VK_ESCAPE] && !keysPrev[VK_ESCAPE]) running = FALSE;
            break;
        case SCENE_INTRO: UpdateIntro(dt); break;
        case SCENE_GAMEPLAY: UpdateGameplay(dt); break;
        default: break;
    }
}

static void RenderGame(void) {
    textQueueCount = 0;

    switch (scene) {
        case SCENE_LOBBY: RenderLobby(); break;
        case SCENE_OPTIONS:
            /* 옵션 팝업이 뜬 동안은 뒤쪽 로비 버튼이 클릭/호버되지 않게 막고, 딤 처리 후 팝업을 그린다 */
            inputBlocked = TRUE;
            RenderLobby();
            inputBlocked = FALSE;
            FillPixelRectAlpha(0, 0, GAME_W, GAME_H, 0x000000, 0.55f);
            RenderOptions();
            break;
        case SCENE_INTRO: RenderIntro(); break;
        case SCENE_GAMEPLAY: RenderGameplay(); break;
    }

    /* 커스텀 픽셀 커서 -- 시스템 커서는 숨기고 직접 그림. 클릭/드래그 가능 지점 위에서는 노란색 */
    uint32_t curColor = cursorHot ? 0xFFFFD020 : 0xFFAAAAAA;
    FillPixelRect(mouseX - 1, mouseY - 4, 2, 9, curColor);
    FillPixelRect(mouseX - 4, mouseY - 1, 9, 2, curColor);
    PutPixel(mouseX, mouseY, 0xFFFFFFFF);
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
        case WM_SETCURSOR:
            /* 클라이언트 영역 안에서는 시스템 커서를 숨긴다 -- 직접 그리는 픽셀 커서만 보이게 */
            if (LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; }
            return DefWindowProc(hwnd, msg, wp, lp);
        default: return DefWindowProc(hwnd, msg, wp, lp);
    }
}

static void FlushTextQueue(void) {
    HFONT fonts[3] = { g_fontSmall, g_fontMedium, g_fontLarge };
    /* HBIOS-SYS는 한글 음절 advance width가 라틴 문자의 2배(터미널 폰트 관례라 1em 정사각
       칸)라, 그대로 두면 음절 사이가 헐렁해 보임("자간이 너무 넓다" 피드백) -- 한국어
       텍스트일 때만 폰트 크기에 비례한 음수 자간으로 타이트하게 당겨준다. 영문은 이미
       라틴 폭(0.5em)이라 그대로 두고, 폴백 폰트(맑은 고딕)일 때도 이 폭 차이가 없으므로
       건드리지 않는다. */
    static const int korCharExtra[3] = { -14, -30, -30 }; /* fontIdx 0/1/2 = 작게/중간/크게 폰트에 대응 -- 작은 폰트는 같은 비율로 줄이면 겹쳐서(픽셀이 적어 여유가 없음) 실측으로 따로 보정함 */
    for (int i = 0; i < textQueueCount; i++) {
        TextCmd *c = &textQueue[i];
        HFONT old = (HFONT)SelectObject(g_hdc, fonts[c->fontIdx]);
        SetTextColor(g_hdc, RGB((c->color >> 16) & 0xFF, (c->color >> 8) & 0xFF, c->color & 0xFF));
        SetTextCharacterExtra(g_hdc, (langIndex == 1 || !g_customFontLoaded) ? 0 : korCharExtra[c->fontIdx]);
        if (c->w > 0) {
            /* DrawTextW(DT_CENTER)는 문자열이 길어질수록 자간(character extra) 반영이
               내부 측정 단계와 어긋나서 글자가 서로 겹치는 버그가 있었음(짧은 문자열은
               괜찮은데 긴 문자열에서 음절이 뭉개짐) -- GetTextExtentPoint32W로 직접
               폭을 재서 TextOutW로 그리면 같은 자간 설정을 측정/출력 양쪽에 똑같이
               적용하므로 항상 정확하게 가운데 정렬됨. */
            SIZE sz;
            GetTextExtentPoint32W(g_hdc, c->text, lstrlenW(c->text), &sz);
            int boxX = c->x * WINDOW_SCALE, boxY = c->y * WINDOW_SCALE;
            int boxW = c->w * WINDOW_SCALE, boxH = c->h * WINDOW_SCALE;
            int drawX = boxX + (boxW - sz.cx) / 2;
            int drawY = boxY + (boxH - sz.cy) / 2;
            TextOutW(g_hdc, drawX, drawY, c->text, lstrlenW(c->text));
        } else {
            TextOutW(g_hdc, c->x * WINDOW_SCALE, c->y * WINDOW_SCALE, c->text, lstrlenW(c->text));
        }
        SelectObject(g_hdc, old);
    }
    textQueueCount = 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)hPrev; (void)cmdLine;
    srand(GetTickCount());

    /* Windows 기본 타이머 해상도(보통 ~15ms)에서는 Sleep(1)이 실제로 훨씬 오래 걸려
       프레임이 낮아 보인다. 1ms 해상도를 요청해서 프레임 페이싱을 촘촘하게 만든다.
       실행파일 크기엔 영향 없음(가져오는 함수 몇 개 추가되는 수준). */
    timeBeginPeriod(1);
    InitAudio();
    InitMusic();
    InitWar();
    InitTick();
    InitVoice();

    char lobbyBgPath[MAX_PATH];
    GetAssetPath(lobbyBgPath, MAX_PATH, "images\\Lobby.bmp");
    lobbyBgLoaded = LoadBmpInto(lobbyBgPath, lobbyBgPixels, GAME_W, GAME_H, 0, 0, NULL, NULL);

    char logoPath[MAX_PATH];
    GetAssetPath(logoPath, MAX_PATH, "images\\Logo.bmp");
    logoLoaded = LoadBmpInto(logoPath, logoPixels, 0, 0, LOGO_MAX_W, LOGO_MAX_H, &logoW, &logoH);

    char voicePath[MAX_PATH];
    GetAssetPath(voicePath, MAX_PATH, "audio\\voice_digits.bin");
    g_voiceLoaded = LoadVoiceDigits(voicePath);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "GameWindowClass";
    wc.hCursor = NULL; /* 커스텀 픽셀 커서를 직접 그리므로 시스템 커서는 WM_SETCURSOR에서 숨김 */
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
    g_screenDC = hdc;

    /* 오프스크린 메모리 DC -- 프레임버퍼 blit과 텍스트 그리기를 전부 여기서 한 뒤,
       화면에는 완성된 그림을 BitBlt로 한 번에만 낸다 (안 그러면 그 사이 순간이
       노출되어 텍스트가 깜빡여 보임). */
    RECT initClient;
    GetClientRect(hwnd, &initClient);
    g_hdc = CreateCompatibleDC(hdc);
    g_memBitmap = CreateCompatibleBitmap(hdc, initClient.right, initClient.bottom);
    g_memBitmapOld = (HBITMAP)SelectObject(g_hdc, g_memBitmap);
    SetBkMode(g_hdc, TRANSPARENT);

    char fontPath[MAX_PATH];
    GetAssetPath(fontPath, MAX_PATH, "fonts\\HBIOS-SYS.ttf");
    g_customFontLoaded = LoadCustomFont(fontPath);
    const wchar_t *uiFontName = g_customFontLoaded ? L"HBIOS-SYS" : Kor("맑은 고딕");

    g_fontSmall  = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        HANGUL_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, uiFontName);
    g_fontMedium = CreateFontW(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        HANGUL_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, uiFontName);
    g_fontLarge  = CreateFontW(-56, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        HANGUL_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, uiFontName);

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
        UpdateMusicLoop();
        UpdateWarLoop();

        RECT client;
        GetClientRect(hwnd, &client);
        StretchDIBits(g_hdc, 0, 0, client.right, client.bottom,
            0, 0, GAME_W, GAME_H, framebuffer, &bmi, DIB_RGB_COLORS, SRCCOPY);
        FlushTextQueue();
        BitBlt(g_screenDC, 0, 0, client.right, client.bottom, g_hdc, 0, 0, SRCCOPY);
        Sleep(1);
    }
    DeleteObject(g_fontSmall);
    DeleteObject(g_fontMedium);
    DeleteObject(g_fontLarge);
    if (g_customFontRes) RemoveFontMemResourceEx(g_customFontRes);
    if (g_waveOutOpen) waveOutClose(g_waveOut);
    if (g_musicOutOpen) { waveOutReset(g_waveOutMusic); waveOutClose(g_waveOutMusic); }
    if (g_warOutOpen) { waveOutReset(g_waveOutWar); waveOutClose(g_waveOutWar); }
    if (g_tickOutOpen) { waveOutReset(g_waveOutTick); waveOutClose(g_waveOutTick); }
    SelectObject(g_hdc, g_memBitmapOld);
    DeleteObject(g_memBitmap);
    DeleteDC(g_hdc);
    ReleaseDC(hwnd, g_screenDC);
    timeEndPeriod(1);
    return 0;
}
