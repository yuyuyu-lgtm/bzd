/* =========================================================
 * main.cpp —— 贪吃蛇 SnackGame++（GameLib C++ 重制版）
 * 移植自 Win32 GDI 版：渐变蛇身 + 蛇头细节 + 背景图 + 音效 + 最高分
 * 编译：g++ main.cpp -o SnackPP.exe -mwindows -std=c++11
 * 操作：方向键 / WASD 移动 · 空格 暂停 · 回车/R 重开 · M 音效 · Esc 退出
 * ========================================================= */
#include "GameLib.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#define CELL     24
#define COLS     24
#define ROWS     24
#define TOPBAR   72
#define BOARD_W  (COLS * CELL)
#define BOARD_H  (ROWS * CELL)
#define WIN_W    BOARD_W
#define WIN_H    (TOPBAR + BOARD_H)
#define MAXLEN   (COLS * ROWS)

#define FONT_CN  "Microsoft YaHei UI"

enum { ST_PLAY = 0, ST_OVER = 1, ST_WIN = 2 };

struct Cell { int x, y; };

/* ---------- 游戏状态 ---------- */
static Cell snake[MAXLEN];
static int   snakeLen;
static int   dirX, dirY;
static Cell  dirQ[4];          /* 方向输入队列 */
static int   dirQLen = 0;
static Cell  food;
static int   score, best;
static int   state = ST_PLAY;
static bool  paused = false;
static double moveTimer = 0.0, moveInterval = 0.12;
static int   breathTick = 0;   /* 每 moves+1（呼吸/舌头动画用） */
static int   animTick = 0;     /* 每帧 +1（60fps 平滑动画）     */

/* ---------- 颜色工具（ARGB） ---------- */

static uint32_t ARGB(int r, int g, int b, int a = 255)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8)  |  (uint32_t)b;
}

static int Clamp255(int v) { return v > 255 ? 255 : v; }

static uint32_t LerpRGB(uint32_t a, uint32_t b, double t)
{
    return ARGB((int)(COLOR_GET_R(a) + (COLOR_GET_R(b) - COLOR_GET_R(a)) * t),
                (int)(COLOR_GET_G(a) + (COLOR_GET_G(b) - COLOR_GET_G(a)) * t),
                (int)(COLOR_GET_B(a) + (COLOR_GET_B(b) - COLOR_GET_B(a)) * t));
}

static uint32_t Shade(uint32_t c, double f)
{
    return ARGB(Clamp255((int)(COLOR_GET_R(c) * f)),
                Clamp255((int)(COLOR_GET_G(c) * f)),
                Clamp255((int)(COLOR_GET_B(c) * f)));
}

static uint32_t SnakeColor(int i)
{
    double t = (snakeLen <= 1) ? 0.0 : (double)i / (snakeLen - 1);
    if (t < 0.5)
        return LerpRGB(ARGB(80, 140, 255), ARGB(165, 110, 255), t * 2.0);
    return LerpRGB(ARGB(165, 110, 255), ARGB(255, 105, 180), (t - 0.5) * 2.0);
}

/* ---------- 最高分持久化（best.dat 存 exe 旁边） ---------- */

static void BestPath(char *path)
{
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    if (slash) *slash = '\0';
    strcat(path, "\\best.dat");
}

static void LoadBest(void)
{
    char path[MAX_PATH];
    FILE *f;
    BestPath(path);
    f = fopen(path, "r");
    if (!f) return;
    if (fscanf(f, "%d", &best) != 1 || best < 0) best = 0;
    fclose(f);
}

/* ---------- 账号模块：注册 / 登录 / 按账号存最高分 ----------
 * 存储在 exe 旁的 accounts.txt，每行：用户名 密码哈希 最高分
 * 哈希用 FNV-1a（教学演示用，不是真正的加密） */

#define ACC_FILE  "accounts.txt"
#define FIELD_MAX 16
#define MAX_ACC   100

struct Acc { char user[32]; unsigned long hash; int best; };

static Acc  g_accs[MAX_ACC];
static int  g_accN = 0;
static bool g_loggedIn = false;
static char g_curUser[32] = "";
static char g_accPath[MAX_PATH * 2];

static unsigned long HashStr(const char *s)
{
    unsigned long h = 1469598103UL;             /* FNV-1a */
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 2166136261UL;
    }
    return h;
}

static void AccPathInit(const char *exeDir)
{
    snprintf(g_accPath, sizeof(g_accPath), "%s\\%s", exeDir, ACC_FILE);
}

static void LoadAccounts(void)
{
    FILE *f = fopen(g_accPath, "r");
    g_accN = 0;
    if (!f) return;
    while (g_accN < MAX_ACC &&
           fscanf(f, "%31s %lu %d",
                  g_accs[g_accN].user, &g_accs[g_accN].hash,
                  &g_accs[g_accN].best) == 3)
        g_accN++;
    fclose(f);
}

static void SaveAccounts(void)
{
    FILE *f = fopen(g_accPath, "w");
    int i;
    if (!f) return;
    for (i = 0; i < g_accN; i++)
        fprintf(f, "%s %lu %d\n",
                g_accs[i].user, g_accs[i].hash, g_accs[i].best);
    fclose(f);
}

static int FindAcc(const char *user)
{
    int i;
    for (i = 0; i < g_accN; i++)
        if (strcmp(g_accs[i].user, user) == 0)
            return i;
    return -1;
}

/* 键盘字符进入编辑框：字母（Shift 大写）+ 数字 + 退格 */
static void EditField(GameLib &game, char *buf, int &len)
{
    int c;
    for (c = 0x30; c <= 0x39; c++) {
        if (game.IsKeyPressed(c) && len < FIELD_MAX)
            buf[len++] = (char)c;
    }
    for (c = 0x41; c <= 0x5A; c++) {
        if (game.IsKeyPressed(c) && len < FIELD_MAX)
            buf[len++] = game.IsKeyDown(KEY_SHIFT) ? (char)c : (char)(c + 32);
    }
    if (game.IsKeyPressed(KEY_BACK) && len > 0)
        len--;
    buf[len] = '\0';
}

static bool TryLogin(const char *user, const char *pass, char *msg)
{
    int i;
    if (!user[0] || !pass[0]) {
        strcpy(msg, "请输入账号和密码");
        return false;
    }
    i = FindAcc(user);
    if (i < 0) {
        strcpy(msg, "账号不存在，请先注册");
        return false;
    }
    if (g_accs[i].hash != HashStr(pass)) {
        strcpy(msg, "密码错误");
        return false;
    }
    g_loggedIn = true;
    strcpy(g_curUser, user);
    best = g_accs[i].best;         /* 该账号自己的最高分 */
    return true;
}

static bool TryRegister(const char *user, const char *pass, char *msg)
{
    if (!user[0] || !pass[0]) {
        strcpy(msg, "请输入账号和密码");
        return false;
    }
    if (FindAcc(user) >= 0) {
        strcpy(msg, "账号已存在");
        return false;
    }
    if (g_accN >= MAX_ACC) {
        strcpy(msg, "账号数量已达上限");
        return false;
    }
    strcpy(g_accs[g_accN].user, user);
    g_accs[g_accN].hash = HashStr(pass);
    g_accs[g_accN].best = 0;
    g_accN++;
    SaveAccounts();
    g_loggedIn = true;
    strcpy(g_curUser, user);
    best = 0;
    return true;
}

static void UpdateBest(void)
{
    char path[MAX_PATH];
    FILE *f;
    if (score <= best)
        return;
    best = score;
    if (g_loggedIn) {              /* 已登录：写入该账号的最高分 */
        int i = FindAcc(g_curUser);
        if (i >= 0) {
            g_accs[i].best = best;
            SaveAccounts();
        }
        return;
    }
    BestPath(path);                /* 游客：best.dat */
    f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", best); fclose(f); }
}

/* ---------- 音效：PlayBeep + 简易旋律调度器 ---------- */

static bool soundOn = true;

struct Pending { double due; int freq, ms; };
static std::vector<Pending> g_notes;
static double g_time = 0.0;

/* 把一段 {频率,毫秒} 旋律排进播放队列（音符依次衔接） */
static void PlayMelody(const int notes[][2], int n)
{
    if (!soundOn) return;
    double t = g_time;
    for (int i = 0; i < n; i++) {
        t += notes[i][1] / 1000.0 * 0.9;   /* 稍微紧凑一点 */
        g_notes.push_back(Pending());
        g_notes.back().due = t;
        g_notes.back().freq = notes[i][0];
        g_notes.back().ms   = notes[i][1];
        t += notes[i][1] / 1000.0 * 0.1;
    }
}

static void PumpMelody(GameLib &game)
{
    while (!g_notes.empty() && g_notes.front().due <= g_time) {
        game.PlayBeep(g_notes.front().freq, g_notes.front().ms);
        g_notes.erase(g_notes.begin());
    }
}

static const int MEL_EAT[][2]   = { {880, 45}, {1319, 45} };
static const int MEL_PAUSE[][2] = { {660, 60} };
static const int MEL_DIE[][2]   = { {392, 90}, {330, 90}, {262, 90},
                                    {196, 90}, {147, 130} };
static const int MEL_WIN[][2]   = { {523, 120}, {659, 120},
                                    {784, 120}, {1047, 220} };

/* ---------- 游戏逻辑 ---------- */

static bool OnSnake(int x, int y)
{
    for (int i = 0; i < snakeLen; i++)
        if (snake[i].x == x && snake[i].y == y)
            return true;
    return false;
}

static void PushDir(int x, int y)
{
    int lx, ly;
    if (dirQLen > 0) { lx = dirQ[dirQLen-1].x; ly = dirQ[dirQLen-1].y; }
    else             { lx = dirX;               ly = dirY; }
    if ((lx == x && ly == y) || (lx == -x && ly == -y))
        return;
    if (dirQLen < 4) {
        dirQ[dirQLen].x = x; dirQ[dirQLen].y = y;
        dirQLen++;
    }
}

static void SpawnFood(void)
{
    if (snakeLen >= MAXLEN) {
        state = ST_WIN;
        UpdateBest();
        PlayMelody(MEL_WIN, 4);
        return;
    }
    int guard = 0;
    do {
        food.x = GameLib::Random(0, COLS - 1);
        food.y = GameLib::Random(0, ROWS - 1);
    } while (OnSnake(food.x, food.y) && ++guard < 20000);
}

static void NewGame(void)
{
    snakeLen = 4;
    for (int i = 0; i < snakeLen; i++) {
        snake[i].x = COLS / 2 - i;
        snake[i].y = ROWS / 2;
    }
    dirX = 1; dirY = 0;
    dirQLen = 0;
    score = 0;
    state = ST_PLAY;
    paused = false;
    moveTimer = 0.0;
    moveInterval = 0.12;
    breathTick = 0;
    SpawnFood();
}

static void Tick(void)
{
    if (state != ST_PLAY || paused)
        return;

    if (dirQLen > 0) {
        int qx = dirQ[0].x, qy = dirQ[0].y;
        for (int j = 1; j < dirQLen; j++)
            dirQ[j - 1] = dirQ[j];
        dirQLen--;
        if (!(qx == -dirX && qy == -dirY)) {
            dirX = qx;
            dirY = qy;
        }
    }

    int nx = snake[0].x + dirX;
    int ny = snake[0].y + dirY;

    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
        state = ST_OVER;
        UpdateBest();
        PlayMelody(MEL_DIE, 5);
        return;
    }
    for (int i = 0; i < snakeLen - 1; i++) {
        if (snake[i].x == nx && snake[i].y == ny) {
            state = ST_OVER;
            UpdateBest();
            PlayMelody(MEL_DIE, 5);
            return;
        }
    }

    for (int i = snakeLen - 1; i > 0; i--)
        snake[i] = snake[i - 1];
    snake[0].x = nx;
    snake[0].y = ny;

    if (nx == food.x && ny == food.y) {
        score += 10;
        PlayMelody(MEL_EAT, 2);
        if (snakeLen < MAXLEN) {
            snake[snakeLen] = snake[snakeLen - 1];
            snakeLen++;
        }
        if (moveInterval > 0.055)
            moveInterval -= 0.003;
        SpawnFood();
    }
    breathTick++;
}

/* ---------- 绘制 ---------- */

/* 背景：等比 cover 缩放（居中裁剪，不变形），找不到则深色兜底 */
static void DrawBgCover(GameLib &game, int bg)
{
    if (bg >= 0) {
        int sw = game.GetSpriteWidth(bg);
        int sh = game.GetSpriteHeight(bg);
        double s = (double)WIN_W / sw;
        if ((double)WIN_H / sh > s)
            s = (double)WIN_H / sh;
        int dw = (int)(sw * s + 0.5);
        int dh = (int)(sh * s + 0.5);
        game.SetClip(0, 0, WIN_W, WIN_H);
        game.DrawSpriteScaled(bg, (WIN_W - dw) / 2, (WIN_H - dh) / 2, dw, dh);
        game.ClearClip();
    } else {
        game.Clear(ARGB(22, 25, 44));
    }
}

static void DrawBoard(GameLib &game, int bg)
{
    DrawBgCover(game, bg);
    if (bg < 0)
        game.DrawTextFont(16, TOPBAR + 10,
                          "背景图未找到（assets/bg.png）",
                          ARGB(255, 90, 90), FONT_CN, 16);

    /* 棋盘区半透明深色 + 顶部玻璃条 */
    game.FillRect(0, TOPBAR, BOARD_W, BOARD_H, ARGB(10, 13, 30, 135));
    game.FillRect(0, 0, WIN_W, TOPBAR, ARGB(16, 19, 40, 190));

    /* 网格 */
    game.DrawGrid(0, TOPBAR, ROWS, COLS, CELL, ARGB(34, 39, 66));
}

static void DrawFood(GameLib &game)
{
    int cx = food.x * CELL + CELL / 2;
    int cy = TOPBAR + food.y * CELL + CELL / 2;
    game.FillCircle(cx, cy, 8, ARGB(255, 92, 158));
    game.FillCircle(cx - 2, cy - 3, 3, ARGB(255, 175, 208));
}

static void DrawSnake(GameLib &game)
{
    int breath = (animTick / 20) % 2;      /* 呼吸脉动：60fps 下约 0.33s 一档 */
    int tongue = (animTick / 12) % 8;      /* 舌头节奏 */

    /* 第 1 层：投影阴影 */
    for (int i = snakeLen - 1; i >= 0; i--) {
        int x = snake[i].x * CELL;
        int y = TOPBAR + snake[i].y * CELL;
        game.FillRect(x + 3, y + 6, CELL - 4, CELL - 5, ARGB(10, 12, 26, 150));
    }

    /* 第 2 层：主体 + 鳞片 + 高光 */
    for (int i = snakeLen - 1; i >= 0; i--) {
        uint32_t c = SnakeColor(i);
        int x = snake[i].x * CELL;
        int y = TOPBAR + snake[i].y * CELL;
        int cx = x + CELL / 2, cy = y + CELL / 2;
        bool isTail = (i == snakeLen - 1);
        bool isHead = (i == 0);

        if (isTail && snakeLen > 1)
            game.FillEllipse(cx, cy, CELL / 2 - 5, CELL / 2 - 5, c);
        else if (isHead)
            game.FillRect(x + 1, y + 1, CELL - 2, CELL - 2, c);
        else
            game.FillRect(x + 2 + breath, y + 2 + breath,
                          CELL - 4 - breath * 2, CELL - 4 - breath * 2, c);

        /* 鳞片：一圈亮色描边椭圆 */
        game.DrawEllipse(cx, cy, CELL / 2 - 4, CELL / 2 - 4, Shade(c, 1.5));

        /* 上方高光条 */
        if (!isHead)
            game.FillRect(x + 5, y + 4, CELL - 10, CELL / 2 - 6, Shade(c, 1.4));
    }

    /* 第 3 层：蛇头细节 */
    {
        int cx = snake[0].x * CELL + CELL / 2;
        int cy = TOPBAR + snake[0].y * CELL + CELL / 2;
        int fx = dirX, fy = dirY;
        int px = -fy, py = fx;

        /* 头顶双高光点 */
        game.FillCircle(cx + fx*2 + px*3, cy + fy*2 + py*3, 3, ARGB(200, 225, 255));
        game.FillCircle(cx + fx*2 - px*3, cy + fy*2 - py*3, 3, ARGB(200, 225, 255));

        /* 鼻孔 */
        game.FillCircle(cx + fx*7 + px*2, cy + fy*7 + py*2, 1, ARGB(22, 24, 48));
        game.FillCircle(cx + fx*7 - px*2, cy + fy*7 - py*2, 1, ARGB(22, 24, 48));

        /* 眼睛：白 → 黑 → 反光 */
        game.FillCircle(cx + fx*3 + px*4, cy + fy*3 + py*4, 3, ARGB(255, 255, 255));
        game.FillCircle(cx + fx*3 - px*4, cy + fy*3 - py*4, 3, ARGB(255, 255, 255));
        game.FillCircle(cx + fx*5 + px*4, cy + fy*5 + py*4, 2, ARGB(20, 22, 38));
        game.FillCircle(cx + fx*5 - px*4, cy + fy*5 - py*4, 2, ARGB(20, 22, 38));
        game.FillCircle(cx + fx*5 + px*4 + 1, cy + fy*5 + py*4 + 1, 1, ARGB(255, 255, 255));
        game.FillCircle(cx + fx*5 - px*4 + 1, cy + fy*5 - py*4 + 1, 1, ARGB(255, 255, 255));

        /* 分岔小舌头 */
        if (state == ST_PLAY && !paused && tongue < 3) {
            int out = 4 + tongue;
            uint32_t tc = ARGB(255, 80, 120);
            game.DrawLine(cx + fx*9, cy + fy*9,
                          cx + fx*(9+out), cy + fy*(9+out), tc);
            game.DrawLine(cx + fx*(9+out) + px*2, cy + fy*(9+out) + py*2,
                          cx + fx*(9+out) - px*2, cy + fy*(9+out) - py*2, tc);
        }
    }
}

static void DrawUI(GameLib &game)
{
    game.DrawPrintfFont(16, 6, ARGB(135, 185, 255), FONT_CN, 48, "得分 %d", score);
    game.DrawPrintfFont(300, 6, ARGB(255, 130, 185), FONT_CN, 48, "最高 %d", best);

    game.DrawTextFont(16, 44,
        soundOn ? "方向键 / WASD 移动   空格 暂停   M 静音   Esc 退出"
                : "音效已关闭 · 按 M 开启   方向键 / WASD 移动   空格 暂停",
        ARGB(140, 148, 185), FONT_CN, 24);

    /* 覆盖层 */
    if (paused && state == ST_PLAY) {
        game.FillRect(0, TOPBAR, BOARD_W, BOARD_H, ARGB(15, 17, 32, 200));
        game.DrawTextFont(BOARD_W/2 - 87, TOPBAR + BOARD_H/2 - 50,
                          "已暂停", ARGB(160, 200, 255), FONT_CN, 58);
        game.DrawTextFont(BOARD_W/2 - 90, TOPBAR + BOARD_H/2 + 40,
                          "按 空格 继续", ARGB(150, 158, 195), FONT_CN, 28);
    } else if (state == ST_OVER || state == ST_WIN) {
        game.FillRect(0, TOPBAR, BOARD_W, BOARD_H, ARGB(15, 17, 32, 210));
        uint32_t tc = (state == ST_WIN) ? ARGB(255, 170, 210) : ARGB(255, 120, 150);
        if (state == ST_WIN) {
            /* 11 个字在 54 号下超宽（594px > 576px），拆两行居中 */
            game.DrawTextFont(BOARD_W/2 - 108, TOPBAR + BOARD_H/2 - 110,
                              "你赢啦，", tc, FONT_CN, 54);
            game.DrawTextFont(BOARD_W/2 - 189, TOPBAR + BOARD_H/2 - 50,
                              "蛇占满了棋盘！", tc, FONT_CN, 54);
        } else {
            game.DrawTextFont(BOARD_W/2 - 135, TOPBAR + BOARD_H/2 - 80,
                              "游戏结束！", tc, FONT_CN, 54);
        }
        game.DrawPrintfFont(BOARD_W/2 - 210, TOPBAR + BOARD_H/2 + 40,
                            ARGB(160, 200, 255), FONT_CN, 30,
                            "本局得分 %d    按 回车 重新开始", score);
    }
}

/* ---------- 登录界面 ---------- */

/* 禁用窗口 IME：键盘输入始终为原始键值，
 * 否则中文输入法会拦截字母键（组词），输入框打不进字 */
static void DisableIME(void)
{
    typedef BOOL (WINAPI *ImmAssocFn)(HWND, void *);
    HWND hwnd = FindWindowW(L"GameLibWindowClass", NULL);
    HMODULE imm;
    ImmAssocFn fn;
    if (!hwnd)
        return;
    imm = LoadLibraryA("imm32.dll");
    if (!imm)
        return;
    fn = (ImmAssocFn)(void *)GetProcAddress(imm, "ImmAssociateContext");
    if (fn)
        fn(hwnd, NULL);
}

/* 自绘按钮：FillRect + 立体边 + 中文系统字体 + 点击检测 */
static bool UiButton(GameLib &game, int x, int y, int w, int h,
                     const char *text, uint32_t face, int fontSize)
{
    bool hover = game.GetMouseX() >= x && game.GetMouseX() < x + w &&
                 game.GetMouseY() >= y && game.GetMouseY() < y + h;
    bool click = hover && game.IsMousePressed(0);

    game.FillRect(x, y, w, h, hover ? Shade(face, 1.15) : face);
    game.DrawRect(x, y, w, h, Shade(face, 1.6));
    game.DrawRect(x + 1, y + 1, w - 2, h - 2, Shade(face, 0.5));
    {
        int tw = game.GetTextWidthFont(text, FONT_CN, fontSize);
        game.DrawTextFont(x + (w - tw) / 2, y + (h - fontSize) / 2 + 2,
                          text, ARGB(255, 255, 255), FONT_CN, fontSize);
    }
    return click;
}

static void DrawField(GameLib &game, int x, int y, int w, int h,
                      const char *label, const char *shown, bool active)
{
    uint32_t border = active ? ARGB(135, 185, 255) : ARGB(70, 76, 110);
    game.DrawTextFont(x, y - 32, label, ARGB(160, 200, 255), FONT_CN, 22);
    game.FillRect(x, y, w, h, ARGB(22, 25, 44, 230));
    game.DrawRect(x, y, w, h, border);
    if (active)
        game.DrawRect(x + 1, y + 1, w - 2, h - 2, border);
    game.DrawTextFont(x + 14, y + (h - 24) / 2 + 2, shown,
                      ARGB(230, 235, 255), FONT_CN, 24);
}

static void LoginScreen(GameLib &game, int bg, bool &quit)
{
    const int FX = 178, FW = 220, FH = 44, FY1 = 252, FY2 = 364;
    char user[FIELD_MAX + 1] = "";
    char pass[FIELD_MAX + 1] = "";
    char pMask[FIELD_MAX + 2];
    char msg[80] = "";
    int ulen = 0, plen = 0, field = 0;     /* 0=账号 1=密码 */
    uint32_t msgColor = ARGB(255, 120, 150);
    bool done = false, blink;
    int i;

    while (!done && !game.IsClosed()) {
        double dt = game.GetDeltaTime();
        g_time += dt;
        animTick++;

        /* 输入 */
        if (game.IsKeyPressed(KEY_ESCAPE)) {
            quit = true;
            break;
        }
        if (game.IsKeyPressed(KEY_TAB))
            field ^= 1;
        if (field == 0) EditField(game, user, ulen);
        else            EditField(game, pass, plen);

        /* 鼠标点击选中输入框 */
        if (game.IsMousePressed(0)) {
            int mx = game.GetMouseX(), my = game.GetMouseY();
            if (mx >= FX && mx < FX + FW && my >= FY1 && my < FY1 + FH)
                field = 0;
            if (mx >= FX && mx < FX + FW && my >= FY2 && my < FY2 + FH)
                field = 1;
        }
        /* Enter = 登录 */
        if (game.IsKeyPressed(KEY_ENTER) && TryLogin(user, pass, msg)) {
            msgColor = ARGB(120, 220, 140);
            PlayMelody(MEL_WIN, 4);
            done = true;
        }

        /* 渲染 */
        DrawBgCover(game, bg);
        game.FillRect(0, 0, WIN_W, WIN_H, ARGB(10, 13, 30, 185));

        game.DrawTextFont(WIN_W/2 - 132, 92, "贪吃蛇 · 登录",
                          ARGB(135, 185, 255), FONT_CN, 44);
        game.DrawTextFont(WIN_W/2 - 172, 150, "SnackGame++ 账号系统",
                          ARGB(140, 148, 185), FONT_CN, 20);

        /* 输入框（密码显示为星号 + 闪烁光标） */
        blink = (animTick / 20) % 2;
        {
            char uText[FIELD_MAX + 2];
            snprintf(uText, sizeof(uText), "%s%s", user,
                     (field == 0 && blink) ? "_" : "");
            for (i = 0; i < plen; i++) pMask[i] = '*';
            pMask[i] = '\0';
            if (field == 1 && blink) strcat(pMask, "_");

            DrawField(game, FX, FY1, FW, FH, "账号", uText, field == 0);
            DrawField(game, FX, FY2, FW, FH, "密码", pMask, field == 1);
        }

        /* 按钮 */
        if (UiButton(game, FX, 434, 100, 46, "登录", ARGB(80, 140, 255), 24)) {
            if (TryLogin(user, pass, msg)) {
                msgColor = ARGB(120, 220, 140);
                PlayMelody(MEL_WIN, 4);
                done = true;
            } else {
                msgColor = ARGB(255, 120, 150);
            }
        }
        if (UiButton(game, FX + 120, 434, 100, 46, "注册", ARGB(165, 110, 255), 24)) {
            if (TryRegister(user, pass, msg)) {
                msgColor = ARGB(120, 220, 140);
                PlayMelody(MEL_WIN, 4);
                done = true;
            } else {
                msgColor = ARGB(255, 120, 150);
            }
        }
        if (UiButton(game, FX, 498, FW, 40, "游客进入", ARGB(90, 96, 130), 20)) {
            g_loggedIn = false;
            done = true;
        }

        if (msg[0])
            game.DrawTextFont(FX, 558, msg, msgColor, FONT_CN, 20);

        game.DrawTextFont(FX, 598,
                          "Tab / 点击切换输入框 · Enter 登录 · 密码显示为 *",
                          ARGB(140, 148, 185), FONT_CN, 15);
        game.DrawTextFont(FX, 622,
                          "账号保存在本地 accounts.txt，密码为哈希存储",
                          ARGB(100, 106, 140), FONT_CN, 13);

        PumpMelody(game);
        game.Update();
        game.WaitFrame(60);
    }
}

/* ---------- 主循环 ---------- */

int main()
{
    GameLib game;
    game.Open(WIN_W, WIN_H, "贪吃蛇 · SnackGame++", true);
    DisableIME();

    srand((unsigned)time(NULL));

    /* 背景图：以 exe 所在目录为基准找（与启动时的工作目录无关） */
    char exeDir[MAX_PATH];
    GetModuleFileNameA(NULL, exeDir, MAX_PATH);
    {
        char *slash = strrchr(exeDir, '\\');
        if (slash) *slash = '\0';
    }
    char p1[MAX_PATH * 2], p2[MAX_PATH * 2];
    snprintf(p1, sizeof(p1), "%s\\assets\\bg.png", exeDir);
    snprintf(p2, sizeof(p2), "%s\\..\\assets\\bg.png", exeDir);

    int bg = game.LoadSprite(p1);
    if (bg < 0) bg = game.LoadSprite(p2);
    if (bg < 0) bg = game.LoadSprite("assets/bg.png");
    if (bg < 0) bg = game.LoadSprite("../assets/bg.png");

    /* 账号系统：登录 / 注册 / 游客 */
    AccPathInit(exeDir);
    LoadAccounts();

    bool quit = false;
    LoginScreen(game, bg, quit);
    if (quit || game.IsClosed())
        return 0;

    if (!g_loggedIn)
        LoadBest();                     /* 游客：读全局 best.dat */
    else {
        char title[96];
        snprintf(title, sizeof(title), "贪吃蛇 · SnackGame++ — %s", g_curUser);
        game.SetTitle(title);
    }
    NewGame();

    while (!game.IsClosed()) {
        if (game.IsKeyPressed(KEY_ESCAPE))
            break;

        double dt = game.GetDeltaTime();
        g_time += dt;
        animTick++;

        /* 输入 */
        if (game.IsKeyPressed(KEY_UP)    || game.IsKeyPressed(KEY_W)) PushDir(0, -1);
        if (game.IsKeyPressed(KEY_DOWN)  || game.IsKeyPressed(KEY_S)) PushDir(0, 1);
        if (game.IsKeyPressed(KEY_LEFT)  || game.IsKeyPressed(KEY_A)) PushDir(-1, 0);
        if (game.IsKeyPressed(KEY_RIGHT) || game.IsKeyPressed(KEY_D)) PushDir(1, 0);
        if (game.IsKeyPressed(KEY_SPACE) && state == ST_PLAY) {
            paused = !paused;
            PlayMelody(MEL_PAUSE, 1);
        }
        if (game.IsKeyPressed(KEY_M)) {
            soundOn = !soundOn;
            if (!soundOn) {
                game.StopAll();
                g_notes.clear();
            }
        }
        if ((game.IsKeyPressed(KEY_ENTER) || game.IsKeyPressed(KEY_R))
            && state != ST_PLAY)
            NewGame();

        /* 逻辑：按 moveInterval 节拍推进 */
        moveTimer += dt;
        if (moveTimer >= moveInterval) {
            moveTimer -= moveInterval;
            Tick();
        }

        /* 到点的音符触发播放 */
        PumpMelody(game);

        /* 渲染 */
        DrawBoard(game, bg);
        DrawFood(game);
        DrawSnake(game);
        DrawUI(game);

        game.Update();
        game.WaitFrame(60);
    }
    return 0;
}
