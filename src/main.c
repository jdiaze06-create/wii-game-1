/*
 * ALLEY SHOWDOWN - 1930s rubber-hose cartoon shooting gallery for Wii
 * All characters are original (a pointy-eared mouse, alley cats, crows, a bunny).
 * devkitPPC + libogc.  Point the Wiimote at the screen.
 *   B = shoot   A = reload   HOME = exit   D-pad = aim if IR is not detected
 * Shoot the cats and crows as they pop up. Do NOT shoot the bunny.
 * If a baddie stays up too long you lose a life.  3 lives.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

#define FIFO_SIZE (256 * 1024)
#define NSLOT 6
#define MAG 6
#define RELOAD_FRAMES 30

#define INK   0x141414FF
#define CREAM 0xF4EBDCFF
#define PINK  0xF0A0A8FF
#define RED   0xC0392BFF

static void *xfb[2] = { NULL, NULL };
static GXRModeObj *rmode = NULL;
static int fb = 0;
static float ct[17], st[17];

/* ---------------------------------------------------------------- state */

enum { ST_TITLE, ST_PLAY, ST_OVER };
enum { T_CAT, T_CROW, T_BUNNY, T_BIGCAT };

typedef struct { int state, type, hp, timer, up, hitt; float p; } Slot;

static Slot g_slot[NSLOT];
static int g_mode = ST_TITLE;
static int g_frame = 0;
static float g_x = 320, g_y = 240;
static int g_ammo = MAG, g_reload = 0, g_score = 0, g_lives = 3;
static int g_level = 1, g_kills = 0, g_flash = 0, g_hurt = 0, g_spawn = 0, g_best = 0;

static const float SX[NSLOT] = { 130, 320, 510, 130, 320, 510 };
static const float SB[NSLOT] = { 200, 200, 200, 340, 340, 340 };   /* crate top */

/* ---------------------------------------------------------- primitives */

static void rect(float x, float y, float w, float h, u32 c) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);         GX_Color1u32(c);
    GX_Position2f32(x + w, y);     GX_Color1u32(c);
    GX_Position2f32(x + w, y + h); GX_Color1u32(c);
    GX_Position2f32(x, y + h);     GX_Color1u32(c);
    GX_End();
}

static void tri(float x1, float y1, float x2, float y2, float x3, float y3, u32 c) {
    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GX_Position2f32(x1, y1); GX_Color1u32(c);
    GX_Position2f32(x2, y2); GX_Color1u32(c);
    GX_Position2f32(x3, y3); GX_Color1u32(c);
    GX_End();
}

static void ell(float cx, float cy, float rx, float ry, u32 c) {
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, 18);
    GX_Position2f32(cx, cy); GX_Color1u32(c);
    for (int i = 0; i < 17; i++) {
        GX_Position2f32(cx + ct[i] * rx, cy + st[i] * ry); GX_Color1u32(c);
    }
    GX_End();
}

static void oell(float cx, float cy, float rx, float ry, u32 c) {
    ell(cx, cy, rx + 3, ry + 3, INK);
    ell(cx, cy, rx, ry, c);
}

static void line(float x1, float y1, float x2, float y2, float th, u32 c) {
    float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.001f) return;
    float nx = -dy / l * th * 0.5f, ny = dx / l * th * 0.5f;
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x1 + nx, y1 + ny); GX_Color1u32(c);
    GX_Position2f32(x2 + nx, y2 + ny); GX_Color1u32(c);
    GX_Position2f32(x2 - nx, y2 - ny); GX_Color1u32(c);
    GX_Position2f32(x1 - nx, y1 - ny); GX_Color1u32(c);
    GX_End();
}

/* 7-segment digits */
static const u8 SEG[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
static void digit(int d, float x, float y, float s, u32 c) {
    u8 m = SEG[d % 10];
    float t = s * 0.2f, w = s * 0.6f, h = s;
    if (m & 0x01) rect(x + t, y,                  w - t, t, c);
    if (m & 0x02) rect(x + w,  y + t,             t, h / 2 - t, c);
    if (m & 0x04) rect(x + w,  y + h / 2,         t, h / 2 - t, c);
    if (m & 0x08) rect(x + t,  y + h - t,         w - t, t, c);
    if (m & 0x10) rect(x,      y + h / 2,         t, h / 2 - t, c);
    if (m & 0x20) rect(x,      y + t,             t, h / 2 - t, c);
    if (m & 0x40) rect(x + t,  y + h / 2 - t / 2, w - t, t, c);
}
static void number(int n, int digs, float x, float y, float s, u32 c) {
    for (int i = digs - 1; i >= 0; i--) { digit(n % 10, x + i * s * 0.85f, y, s, c); n /= 10; }
}

/* tiny 3x5 block font */
static const char FONT_CH[] = "ACDEGHLMNOPRSVWY";
static const u8 FONT[16][5] = {
    {2,5,7,5,5},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{3,4,5,5,3},{5,5,7,5,5},
    {4,4,4,4,7},{5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},{6,5,6,4,4},{6,5,6,5,5},
    {3,4,2,1,6},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,2,2}
};
static void text(const char *s, float x, float y, float px, u32 c) {
    for (; *s; s++, x += px * 4) {
        if (*s == ' ') continue;
        const char *f = strchr(FONT_CH, *s);
        if (!f) continue;
        int g = (int)(f - FONT_CH);
        for (int r = 0; r < 5; r++)
            for (int k = 0; k < 3; k++)
                if (FONT[g][r] & (4 >> k)) rect(x + k * px, y + r * px, px, px, c);
    }
}
static float text_w(const char *s, float px) { return strlen(s) * px * 4 - px; }
static void ctext(const char *s, float y, float px, u32 c) { text(s, 320 - text_w(s, px) / 2, y, px, c); }

/* -------------------------------------------------------------- init */

static void InitVideo(void) {
    VIDEO_Init();
    WPAD_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb[fb]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

static void InitGX(void) {
    for (int i = 0; i < 17; i++) { ct[i] = cosf(i * 2.0f * M_PI / 16.0f); st[i] = sinf(i * 2.0f * M_PI / 16.0f); }

    void *fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);

    GXColor bg = { 0xF2, 0xE8, 0xCF, 0xFF };
    GX_SetCopyClear(bg, GX_MAX_Z24);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfbHeight = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfbHeight);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering,
                    (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    if (rmode->aa) GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
    else           GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_CopyDisp(xfb[fb], GX_TRUE);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetNumChans(1);
    GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

    Mtx44 proj;
    guOrtho(proj, 0, 479, 0, 639, 0, 1);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    Mtx mv;
    guMtxIdentity(mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
}

/* ------------------------------------------------------- characters */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void pupils(float x, float y, float rx, float ry, float r) {
    float dx = clampf((g_x - x) / 60.0f, -1, 1) * r;
    float dy = clampf((g_y - y) / 60.0f, -1, 1) * r;
    ell(x + dx, y + dy, rx * 0.45f, ry * 0.45f, INK);
}

static void xeyes(float x, float y, float s) {
    line(x - 20 * s, y - 14 * s, x - 6 * s, y,  3, INK);
    line(x - 6 * s,  y - 14 * s, x - 20 * s, y, 3, INK);
    line(x + 6 * s,  y - 14 * s, x + 20 * s, y, 3, INK);
    line(x + 20 * s, y - 14 * s, x + 6 * s,  y, 3, INK);
}

static void stars(float x, float y) {
    for (int i = 0; i < 3; i++) {
        float a = g_frame * 0.25f + i * 2.094f;
        ell(x + cosf(a) * 44, y - 52 + sinf(a) * 10, 6, 6, 0xF2C230FF);
        ell(x + cosf(a) * 44, y - 52 + sinf(a) * 10, 3, 3, INK);
    }
}

static void drawCat(float x, float y, int hit, int big, int shake) {
    float s = big ? 1.2f : 1.0f;
    u32 fur = big ? 0x2B2B2BFF : 0x585858FF;
    x += shake ? ((g_frame & 1) ? 5 : -5) : 0;
    oell(x, y + 50 * s, 40 * s, 24 * s, fur);                         /* shoulders */
    if (big) tri(x - 14, y + 34 * s, x + 14, y + 34 * s, x, y + 56 * s, RED);   /* neckerchief */
    tri(x - 40 * s, y - 6 * s, x - 34 * s, y - 52 * s, x - 6 * s, y - 28 * s, INK);
    tri(x + 40 * s, y - 6 * s, x + 34 * s, y - 52 * s, x + 6 * s, y - 28 * s, INK);
    tri(x - 33 * s, y - 12 * s, x - 31 * s, y - 42 * s, x - 14 * s, y - 27 * s, PINK);
    tri(x + 33 * s, y - 12 * s, x + 31 * s, y - 42 * s, x + 14 * s, y - 27 * s, PINK);
    oell(x, y, 40 * s, 34 * s, fur);
    if (hit) {
        xeyes(x, y - 2, s);
    } else {
        oell(x - 15 * s, y - 6 * s, 10 * s, 12 * s, 0xFFF6D8FF);
        oell(x + 15 * s, y - 6 * s, 10 * s, 12 * s, 0xFFF6D8FF);
        pupils(x - 15 * s, y - 6 * s, 10 * s, 12 * s, 4);
        pupils(x + 15 * s, y - 6 * s, 10 * s, 12 * s, 4);
        line(x - 28 * s, y - 22 * s, x - 6 * s, y - 14 * s, 4, INK);           /* angry brows */
        line(x + 28 * s, y - 22 * s, x + 6 * s, y - 14 * s, 4, INK);
    }
    tri(x - 6 * s, y + 8 * s, x + 6 * s, y + 8 * s, x, y + 15 * s, PINK);
    line(x, y + 15 * s, x - 10 * s, y + 24 * s, 3, INK);
    line(x, y + 15 * s, x + 10 * s, y + 24 * s, 3, INK);
    for (int i = -1; i <= 1; i += 2) {
        line(x + i * 22 * s, y + 12 * s, x + i * 56 * s, y + 6 * s, 2, INK);
        line(x + i * 22 * s, y + 16 * s, x + i * 56 * s, y + 18 * s, 2, INK);
    }
}

static void drawCrow(float x, float y, int hit, int shake) {
    x += shake ? ((g_frame & 1) ? 5 : -5) : 0;
    oell(x, y + 50, 38, 22, 0x222222FF);
    tri(x - 10, y - 30, x, y - 58, x + 4, y - 28, INK);                        /* head tuft */
    tri(x + 2, y - 30, x + 16, y - 54, x + 14, y - 26, INK);
    oell(x, y, 36, 32, 0x2E2E2EFF);
    if (hit) {
        xeyes(x, y - 2, 1.0f);
    } else {
        oell(x - 13, y - 8, 9, 10, 0xFFFFFFFF);
        oell(x + 13, y - 8, 9, 10, 0xFFFFFFFF);
        pupils(x - 13, y - 8, 9, 10, 3);
        pupils(x + 13, y - 8, 9, 10, 3);
    }
    tri(x - 12, y + 6, x + 12, y + 6, x, y + 34, INK);
    tri(x - 9, y + 8, x + 9, y + 8, x, y + 30, 0xE08A1EFF);                    /* beak */
}

static void drawBunny(float x, float y, int hit, int shake) {
    x += shake ? ((g_frame & 1) ? 5 : -5) : 0;
    oell(x, y + 50, 36, 22, 0xFFFFFFFF);
    oell(x - 16, y - 50, 9, 26, 0xFFFFFFFF);
    oell(x + 16, y - 50, 9, 26, 0xFFFFFFFF);
    ell(x - 16, y - 48, 4, 18, PINK);
    ell(x + 16, y - 48, 4, 18, PINK);
    oell(x, y, 36, 32, 0xFFFFFFFF);
    if (hit) {
        xeyes(x, y - 2, 0.9f);
    } else {
        oell(x - 12, y - 6, 8, 11, 0xFFFFFFFF);
        oell(x + 12, y - 6, 8, 11, 0xFFFFFFFF);
        pupils(x - 12, y - 6, 8, 11, 3);
        pupils(x + 12, y - 6, 8, 11, 3);
        line(x - 22, y - 20, x - 6, y - 24, 3, INK);                           /* worried brows */
        line(x + 22, y - 20, x + 6, y - 24, 3, INK);
    }
    ell(x, y + 10, 6, 4, PINK);
    ell(x - 22, y + 12, 6, 4, 0xF6C0C4FF);
    ell(x + 22, y + 12, 6, 4, 0xF6C0C4FF);
}

/* the player's mouse: pointy ears, long snout, bow tie */
static void drawMouse(float x, float y, float s, int blink) {
    tri(x - 42 * s, y - 8 * s, x - 36 * s, y - 66 * s, x - 6 * s, y - 32 * s, INK);
    tri(x + 42 * s, y - 8 * s, x + 36 * s, y - 66 * s, x + 6 * s, y - 32 * s, INK);
    tri(x - 35 * s, y - 14 * s, x - 33 * s, y - 52 * s, x - 14 * s, y - 30 * s, PINK);
    tri(x + 35 * s, y - 14 * s, x + 33 * s, y - 52 * s, x + 14 * s, y - 30 * s, PINK);
    ell(x, y, 38 * s + 3, 34 * s + 3, INK);
    ell(x, y, 38 * s, 34 * s, CREAM);
    ell(x, y + 16 * s, 24 * s + 3, 16 * s + 3, INK);
    ell(x, y + 16 * s, 24 * s, 16 * s, CREAM);
    ell(x, y + 8 * s, 8 * s, 6 * s, INK);
    if (blink) {
        line(x - 22 * s, y - 6 * s, x - 6 * s, y - 6 * s, 3 * s + 1, INK);
        line(x + 6 * s,  y - 6 * s, x + 22 * s, y - 6 * s, 3 * s + 1, INK);
    } else {
        oell(x - 13 * s, y - 8 * s, 8 * s, 11 * s, 0xFFFFFFFF);
        oell(x + 13 * s, y - 8 * s, 8 * s, 11 * s, 0xFFFFFFFF);
        pupils(x - 13 * s, y - 8 * s, 8 * s, 11 * s, 3 * s);
        pupils(x + 13 * s, y - 8 * s, 8 * s, 11 * s, 3 * s);
    }
    tri(x - 22 * s, y + 42 * s, x - 22 * s, y + 62 * s, x, y + 52 * s, RED);   /* bow tie */
    tri(x + 22 * s, y + 42 * s, x + 22 * s, y + 62 * s, x, y + 52 * s, RED);
    ell(x, y + 52 * s, 5 * s, 5 * s, INK);
}

/* ----------------------------------------------------------- game */

static void ResetGame(void) {
    memset(g_slot, 0, sizeof(g_slot));
    g_score = 0; g_lives = 3; g_level = 1; g_kills = 0;
    g_ammo = MAG; g_reload = 0; g_flash = 0; g_hurt = 0; g_spawn = 30;
}

static int Active(void) {
    int n = 0;
    for (int i = 0; i < NSLOT; i++) n += (g_slot[i].state != 0);
    return n;
}

static void SpawnSlot(void) {
    int freeIdx[NSLOT], n = 0;
    for (int i = 0; i < NSLOT; i++) if (g_slot[i].state == 0) freeIdx[n++] = i;
    if (!n) return;
    Slot *s = &g_slot[freeIdx[rand() % n]];
    int r = rand() % 100;
    memset(s, 0, sizeof(*s));
    s->state = 1;
    s->type = r < 20 ? T_BUNNY : (r < 58 ? T_CAT : (r < 84 ? T_CROW : T_BIGCAT));
    s->hp = (s->type == T_BIGCAT) ? 2 : 1;
    s->up = 110 - g_level * 9;
    if (s->up < 36) s->up = 36;
}

static void LoseLife(void) {
    g_lives--;
    g_hurt = 10;
    if (g_lives <= 0) {
        g_lives = 0;
        if (g_score > g_best) g_best = g_score;
        g_mode = ST_OVER;
    }
}

static void Fire(void) {
    if (g_reload > 0 || g_ammo <= 0) return;
    g_ammo--;
    g_flash = 3;
    for (int i = NSLOT - 1; i >= 0; i--) {                    /* front row first */
        Slot *s = &g_slot[i];
        if ((s->state != 1 && s->state != 2) || s->p < 0.35f) continue;
        float hx = SX[i], hy = SB[i] + 70 - s->p * 100;
        float dx = g_x - hx, dy = g_y - hy;
        if (dx * dx + dy * dy > 50.0f * 50.0f) continue;
        if (s->type == T_BUNNY) {
            s->state = 4; s->timer = 30; s->hitt = 1;
            LoseLife();
        } else if (--s->hp <= 0) {
            s->state = 4; s->timer = 30; s->hitt = 1;
            g_score += (s->type == T_BIGCAT) ? 300 : 100;
            if (g_score > 99999) g_score = 99999;
            if (++g_kills % 8 == 0) g_level++;
        } else {
            s->hitt = 8;
        }
        return;
    }
}

static int Update(void) {
    WPAD_ScanPads();
    u32 pressed = WPAD_ButtonsDown(0), held = WPAD_ButtonsHeld(0);
    if (pressed & WPAD_BUTTON_HOME) return 0;

    ir_t ir;
    WPAD_IR(0, &ir);
    if (ir.valid) { g_x = ir.x; g_y = ir.y; }
    else {
        if (held & WPAD_BUTTON_LEFT)  g_x -= 7;
        if (held & WPAD_BUTTON_RIGHT) g_x += 7;
        if (held & WPAD_BUTTON_UP)    g_y -= 7;
        if (held & WPAD_BUTTON_DOWN)  g_y += 7;
    }
    g_x = clampf(g_x, 0, 639);
    g_y = clampf(g_y, 0, 479);

    if (g_mode != ST_PLAY) {
        if (pressed & (WPAD_BUTTON_A | WPAD_BUTTON_B | WPAD_BUTTON_PLUS)) { ResetGame(); g_mode = ST_PLAY; }
        return 1;
    }

    if (pressed & WPAD_BUTTON_B) Fire();
    if ((pressed & WPAD_BUTTON_A) && g_reload == 0 && g_ammo < MAG) g_reload = RELOAD_FRAMES;
    if (g_reload > 0 && --g_reload == 0) g_ammo = MAG;
    if (g_flash > 0) g_flash--;
    if (g_hurt > 0) g_hurt--;

    int maxActive = 2 + g_level / 2; if (maxActive > 5) maxActive = 5;
    int interval = 70 - g_level * 6;  if (interval < 18) interval = 18;
    if (--g_spawn <= 0) {
        if (Active() < maxActive) SpawnSlot();
        g_spawn = interval / 2 + rand() % interval;
    }

    float rise = 0.08f + g_level * 0.004f;
    for (int i = 0; i < NSLOT; i++) {
        Slot *s = &g_slot[i];
        if (s->hitt > 0 && s->state != 4) s->hitt--;
        switch (s->state) {
            case 1: s->p += rise; if (s->p >= 1) { s->p = 1; s->state = 2; s->timer = s->up; } break;
            case 2:
                if (--s->timer <= 0) {
                    if (s->type == T_BUNNY) g_score += 50;       /* bunny safely left */
                    else LoseLife();                              /* baddie got you */
                    s->state = 3;
                }
                break;
            case 3: s->p -= rise; if (s->p <= 0) s->state = 0; break;
            case 4: s->p -= 0.03f; if (--s->timer <= 0 || s->p <= 0) s->state = 0; break;
        }
    }
    return 1;
}

/* ------------------------------------------------------------ draw */

static void DrawBackground(void) {
    for (int r = 0; r < 14; r++) {
        float y = 60 + r * 30;
        rect(0, y, 640, 2, 0xD8CBA8FF);
        for (int c = 0; c < 11; c++) rect(c * 64 + ((r & 1) ? 32 : 0), y, 2, 30, 0xD8CBA8FF);
    }
    rect(0, 0, 640, 60, 0xE8DCC0FF);
}

static void DrawSlotChar(int i) {
    Slot *s = &g_slot[i];
    if (s->state == 0) return;
    float x = SX[i], y = SB[i] + 70 - s->p * 100 + sinf(g_frame * 0.25f + i) * 2;
    int hit = (s->state == 4), shake = (s->hitt > 0 && !hit);
    switch (s->type) {
        case T_CAT:    drawCat(x, y, hit, 0, shake); break;
        case T_BIGCAT: drawCat(x, y, hit, 1, shake); break;
        case T_CROW:   drawCrow(x, y, hit, shake); break;
        case T_BUNNY:  drawBunny(x, y, hit, shake); break;
    }
    if (hit) stars(x, y);
}

static void DrawCrate(int i) {
    float x = SX[i] - 62, y = SB[i];
    rect(x - 3, y - 3, 130, 106, INK);
    rect(x, y, 124, 100, 0xC9A66BFF);
    for (int k = 1; k < 4; k++) rect(x, y + k * 25, 124, 3, 0x8C6B3AFF);
    line(x + 4, y + 4, x + 120, y + 96, 5, 0x8C6B3AFF);
    line(x + 120, y + 4, x + 4, y + 96, 5, 0x8C6B3AFF);
}

static void DrawHud(void) {
    number(g_score, 5, 20, 12, 34, INK);
    text("LV", 250, 16, 5, INK);
    number(g_level, 2, 292, 12, 34, INK);
    for (int i = 0; i < g_lives; i++) drawMouse(520 + i * 40, 30, 0.34f, 0);
    for (int i = 0; i < MAG; i++) {                                /* bullets */
        u32 c = (i < g_ammo) ? INK : 0xB8AC90FF;
        ell(36 + i * 22, 455, 7, 9, c);
    }
    if (g_reload > 0) rect(36, 470, 130.0f * (RELOAD_FRAMES - g_reload) / RELOAD_FRAMES, 5, RED);
}

static void DrawCrosshair(void) {
    float cx = g_x, cy = g_y;
    /* gloved hand + pea-shooter from the bottom of the screen toward the aim point */
    float dx = cx - 320, dy = cy - 520, l = sqrtf(dx * dx + dy * dy);
    if (l > 1) {
        float ex = 320 + dx / l * 70, ey = 520 + dy / l * 70;
        line(320, 520, ex, ey, 14, INK);
        line(320, 520, ex, ey, 8, 0x7A5A32FF);
        ell(320 + dx / l * 36, 520 + dy / l * 36, 15, 13, 0xFFFFFFFF);
    }
    u32 cc = (g_flash > 0) ? RED : INK;
    ell(cx, cy, 20, 20, cc);
    ell(cx, cy, 15, 15, (g_flash > 0) ? 0xF2C230FF : 0xF4EBDC00 | 0xA0);
    rect(cx - 30, cy - 1, 16, 3, cc);
    rect(cx + 14, cy - 1, 16, 3, cc);
    rect(cx - 1, cy - 30, 3, 16, cc);
    rect(cx - 1, cy + 14, 3, 16, cc);
    rect(cx - 2, cy - 2, 5, 5, RED);
}

static void DrawFilm(void) {
    for (int i = 0; i < 10; i++) rect(rand() % 640, rand() % 480, 2, 2 + rand() % 3, 0x14141440);
    if ((g_frame % 90) < 2) rect(0, 0, 640, 480, 0x14141410);
    rect(0, 0, 640, 14, 0x14141430);                       /* vignette */
    rect(0, 466, 640, 14, 0x14141430);
    rect(0, 0, 14, 480, 0x14141430);
    rect(626, 0, 14, 480, 0x14141430);
    if (g_hurt > 0) rect(0, 0, 640, 480, 0xC0392B50);
}

static void Draw(void) {
    DrawBackground();

    if (g_mode == ST_TITLE) {
        drawMouse(320, 235, 2.2f, (g_frame % 140) < 6);
        ctext("ALLEY", 52, 14, INK);
        ctext("SHOWDOWN", 130, 8, RED);
        if ((g_frame / 30) & 1) ctext("PRESS A", 418, 7, INK);
        if (g_best > 0) { text("BEST", 20, 440, 5, INK); number(g_best, 5, 100, 436, 30, INK); }
        DrawFilm();
        DrawCrosshair();
        return;
    }

    for (int row = 0; row < 2; row++) {
        for (int i = row * 3; i < row * 3 + 3; i++) DrawSlotChar(i);
        for (int i = row * 3; i < row * 3 + 3; i++) DrawCrate(i);
    }
    DrawHud();

    if (g_mode == ST_OVER) {
        rect(0, 150, 640, 190, 0x141414D0);
        ctext("GAME OVER", 168, 10, 0xF4EBDCFF);
        text("SCORE", 190, 270, 6, 0xF4EBDCFF);
        number(g_score, 5, 330, 264, 36, 0xF2C230FF);
        if ((g_frame / 30) & 1) ctext("PRESS A", 316, 4, 0xF4EBDCFF);
    }
    DrawFilm();
    DrawCrosshair();
}

int main(int argc, char **argv) {
    InitVideo();
    InitGX();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_0, 640, 480);
    srand(0xC0FFEE);
    ResetGame();

    while (Update()) {
        GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
        Draw();
        GX_SetColorUpdate(GX_TRUE);
        GX_CopyDisp(xfb[fb], GX_TRUE);
        GX_DrawDone();
        VIDEO_SetNextFramebuffer(xfb[fb]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        fb ^= 1;
        g_frame++;
    }

    WPAD_Shutdown();
    return 0;
}
