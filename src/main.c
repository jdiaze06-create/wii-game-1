/*
 * DEAD LIGHT - 3D horror rail shooter for Nintendo Wii (devkitPPC + libogc)
 * Original creatures, original everything.
 *
 * You are walking a dead facility with a gun-mounted flashlight.  The dark is
 * only pushed back where you point.  Things come out of it.
 *
 *   Wiimote pointer = aim the flashlight/gun     B = shoot
 *   A = reload                                    HOME = exit
 *   D-pad aims if the IR pointer is not detected
 *
 * Headshots do triple damage.  Things that reach you hurt, and scare you.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <asndlib.h>

#define FIFO_SIZE (256 * 1024)
#define F_LEN     415.7f          /* focal length in pixels for 60deg vertical FOV @480 */
#define FLOOR_Y   (-1.7f)
#define CEIL_Y    2.8f
#define SEG_LEN   8.0f
#define NSEG      11
#define MAXE      10
#define MAG       8
#define RELOAD_F  55
#define SRATE     22050

static void *xfb[2] = { NULL, NULL };
static GXRModeObj *rmode = NULL;
static int fb = 0;
static float ct[17], st[17];

enum { ST_TITLE, ST_PLAY, ST_OVER };
enum { E_GAUNT, E_CRAWLER, E_BRUTE };

typedef struct { float x, z, speed, phase; int active, type, hp, hit; } Enemy;

static Enemy g_e[MAXE];
static int   g_mode = ST_TITLE, g_frame = 0;
static float g_x = 320, g_y = 240;
static float g_ax = 0, g_ay = 0, g_az = -1;      /* aim direction (unit) */
static float g_travel = 0, g_flick = 1, g_beamf = 1, g_pulse = 0;
static int   g_health = 100, g_ammo = MAG, g_reload = 0, g_cool = 0;
static int   g_score = 0, g_best = 0, g_kills = 0, g_wave = 1, g_spawn = 60;
static int   g_shake = 0, g_hurt = 0, g_scare = 0, g_muzzle = 0, g_heart = 0;

/* --------------------------------------------------------------- sound */

static s16 *sn_shot, *sn_scare, *sn_drone, *sn_heart, *sn_click;
static int  n_shot = 5500, n_scare = 13000, n_drone = SRATE * 2, n_heart = 6600, n_click = 900;

static int sbytes(int n) { return ((n * 2 + 31) / 32) * 32; }
static s16 *salloc(int n) {
    int b = sbytes(n);
    s16 *p = (s16 *)memalign(32, b);
    memset(p, 0, b);
    return p;
}
static s16 clip16(float v) { return (s16)(v > 32000 ? 32000 : (v < -32000 ? -32000 : v)); }
static float noise(void) { return (rand() % 65536 - 32768) / 32768.0f; }

static void InitSound(void) {
    ASND_Init();
    ASND_Pause(0);

    sn_shot = salloc(n_shot);
    for (int i = 0; i < n_shot; i++) {
        float env = expf(-i / 900.0f);
        sn_shot[i] = clip16((noise() * 0.7f + sinf(i * 0.045f) * 0.6f) * env * 30000);
    }
    sn_scare = salloc(n_scare);
    float ph = 0;
    for (int i = 0; i < n_scare; i++) {
        float t = (float)i / SRATE, f = 500 + 2200 * t;
        ph += 2 * M_PI * f / SRATE;
        float env = 1.0f - t / 0.59f; if (env < 0) env = 0;
        sn_scare[i] = clip16(((sinf(ph) > 0 ? 1.0f : -1.0f) * 0.35f + noise() * 0.4f) * env * 30000);
    }
    sn_drone = salloc(n_drone);
    for (int i = 0; i < n_drone; i++) {
        float t = (float)i / SRATE;
        float s = 0.5f * sinf(2 * M_PI * 55 * t) + 0.35f * sinf(2 * M_PI * 58 * t)
                + 0.2f * sinf(2 * M_PI * 110 * t);
        s *= 0.75f + 0.25f * sinf(2 * M_PI * t / 2.0f);
        sn_drone[i] = clip16(s * 12000);
    }
    sn_heart = salloc(n_heart);
    for (int i = 0; i < n_heart; i++) {
        float t = (float)i / SRATE, t2 = t - 0.15f;
        float s = sinf(2 * M_PI * 48 * t) * expf(-t * 18.0f);
        if (t2 > 0) s += 0.6f * sinf(2 * M_PI * 44 * t2) * expf(-t2 * 20.0f);
        sn_heart[i] = clip16(s * 26000);
    }
    sn_click = salloc(n_click);
    for (int i = 0; i < n_click; i++) sn_click[i] = clip16(noise() * expf(-i / 150.0f) * 22000);

    DCFlushRange(sn_shot, sbytes(n_shot));
    DCFlushRange(sn_scare, sbytes(n_scare));
    DCFlushRange(sn_drone, sbytes(n_drone));
    DCFlushRange(sn_heart, sbytes(n_heart));
    DCFlushRange(sn_click, sbytes(n_click));

    ASND_SetInfiniteVoice(0, VOICE_MONO16, SRATE, 0, sn_drone, sbytes(n_drone), 90, 90);
}

static void play(int voice, s16 *buf, int n, int vol) {
    ASND_StopVoice(voice);
    ASND_SetVoice(voice, VOICE_MONO16, SRATE, 0, buf, sbytes(n), vol, vol, NULL);
}

/* ------------------------------------------------------ light + 3D prims */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static unsigned hsh(int k) {
    unsigned h = (unsigned)k * 2654435761u;
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    return h;
}

static void lightc(float x, float y, float z, float *r, float *g, float *b) {
    float d = sqrtf(x * x + y * y + z * z);
    float amb = 0.15f * g_flick / (1.0f + d * 0.28f);
    float beam = 0;
    if (d > 0.3f) {
        float c = (x * g_ax + y * g_ay + z * g_az) / d;
        if (c > 0.955f) {
            float k = (c - 0.955f) / 0.045f;
            float fall = 1.0f - d / 36.0f; if (fall < 0) fall = 0;
            beam = k * k * fall * 1.15f * g_beamf;
        }
    }
    float red = g_pulse * 0.10f / (1.0f + d * 0.15f);
    *r = amb + beam + red;
    *g = amb + beam * 0.95f;
    *b = amb * 1.1f + beam * 0.85f;
}

static u32 pack(float r, float g, float b) {
    u32 R = (u32)(clampf(r, 0, 1) * 255), G = (u32)(clampf(g, 0, 1) * 255), B = (u32)(clampf(b, 0, 1) * 255);
    return (R << 24) | (G << 16) | (B << 8) | 0xFF;
}

static void vtx(float x, float y, float z, float br, float bg, float bb) {
    float lr, lg, lb;
    lightc(x, y, z, &lr, &lg, &lb);
    GX_Position3f32(x, y, z);
    GX_Color1u32(pack(br * lr, bg * lg, bb * lb));
}

static void quad3(const float *p, float r, float g, float b) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (int i = 0; i < 4; i++) vtx(p[i * 3], p[i * 3 + 1], p[i * 3 + 2], r, g, b);
    GX_End();
}

static void quadU(const float *p, u32 c) {            /* unlit / emissive */
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (int i = 0; i < 4; i++) { GX_Position3f32(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]); GX_Color1u32(c); }
    GX_End();
}

static void grid(float ox, float oy, float oz, float ux, float uy, float uz,
                 float vx, float vy, float vz, int nu, int nv, float r, float g, float b) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, nu * nv * 4);
    for (int j = 0; j < nv; j++)
        for (int i = 0; i < nu; i++) {
            float a0 = (float)i / nu, a1 = (float)(i + 1) / nu;
            float b0 = (float)j / nv, b1 = (float)(j + 1) / nv;
            vtx(ox + ux * a0 + vx * b0, oy + uy * a0 + vy * b0, oz + uz * a0 + vz * b0, r, g, b);
            vtx(ox + ux * a1 + vx * b0, oy + uy * a1 + vy * b0, oz + uz * a1 + vz * b0, r, g, b);
            vtx(ox + ux * a1 + vx * b1, oy + uy * a1 + vy * b1, oz + uz * a1 + vz * b1, r, g, b);
            vtx(ox + ux * a0 + vx * b1, oy + uy * a0 + vy * b1, oz + uz * a0 + vz * b1, r, g, b);
        }
    GX_End();
}

static void box(float cx, float cy, float cz, float w, float h, float d, float r, float g, float b) {
    float x0 = cx - w / 2, x1 = cx + w / 2, y0 = cy - h / 2, y1 = cy + h / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    if (z1 < 0) { float p[12] = { x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1 }; quad3(p, r * .9f, g * .9f, b * .9f); }
    if (y1 < 0) { float p[12] = { x0,y1,z0, x1,y1,z0, x1,y1,z1, x0,y1,z1 }; quad3(p, r, g, b); }
    if (y0 > 0) { float p[12] = { x0,y0,z0, x1,y0,z0, x1,y0,z1, x0,y0,z1 }; quad3(p, r * .6f, g * .6f, b * .6f); }
    if (x0 > 0) { float p[12] = { x0,y0,z0, x0,y0,z1, x0,y1,z1, x0,y1,z0 }; quad3(p, r * .65f, g * .65f, b * .65f); }
    if (x1 < 0) { float p[12] = { x1,y0,z0, x1,y0,z1, x1,y1,z1, x1,y1,z0 }; quad3(p, r * .65f, g * .65f, b * .65f); }
}

static void glow(float cx, float cy, float z, float w, float h, u32 c) {
    float p[12] = { cx - w, cy - h, z, cx + w, cy - h, z, cx + w, cy + h, z, cx - w, cy + h, z };
    quadU(p, c);
}

/* -------------------------------------------------------------- world */

static void DrawWorld(void) {
    float off = fmodf(g_travel, SEG_LEN);
    int base = (int)(g_travel / SEG_LEN);
    for (int i = 0; i < NSEG; i++) {
        int k = base + i;
        unsigned hv = hsh(k);
        float z0 = -(i * SEG_LEN) + off;
        float tile = (k & 1) ? 0.20f : 0.14f;

        grid(-3, FLOOR_Y, z0, 6, 0, 0, 0, 0, -SEG_LEN, 3, 4, tile, tile, tile * 1.05f);          /* floor   */
        grid(-3, CEIL_Y,  z0, 6, 0, 0, 0, 0, -SEG_LEN, 3, 4, 0.12f, 0.12f, 0.13f);               /* ceiling */
        grid(-3, FLOOR_Y, z0, 0, CEIL_Y - FLOOR_Y, 0, 0, 0, -SEG_LEN, 3, 4, 0.16f, 0.20f, 0.18f); /* left    */
        grid( 3, FLOOR_Y, z0, 0, CEIL_Y - FLOOR_Y, 0, 0, 0, -SEG_LEN, 3, 4, 0.16f, 0.20f, 0.18f); /* right   */

        box(-2.7f, (FLOOR_Y + CEIL_Y) / 2, z0 - 0.3f, 0.6f, CEIL_Y - FLOOR_Y, 0.6f, 0.22f, 0.22f, 0.22f);
        box( 2.7f, (FLOOR_Y + CEIL_Y) / 2, z0 - 0.3f, 0.6f, CEIL_Y - FLOOR_Y, 0.6f, 0.22f, 0.22f, 0.22f);

        if (hv % 3 == 0) {                                   /* door on the left wall */
            grid(-2.98f, FLOOR_Y, z0 - 2.5f, 0, 2.9f, 0, 0, 0, -3.0f, 2, 3, 0.03f, 0.03f, 0.03f);
            float p[12] = { -2.97f, 0.8f, z0 - 3.9f, -2.97f, 0.8f, z0 - 4.1f,
                            -2.97f, 1.0f, z0 - 4.1f, -2.97f, 1.0f, z0 - 3.9f };
            quadU(p, pack(0.6f * g_flick, 0.04f, 0.02f));
        }
        if (hv % 5 == 1) {                                   /* door on the right wall */
            grid(2.98f, FLOOR_Y, z0 - 4.0f, 0, 2.9f, 0, 0, 0, -3.0f, 2, 3, 0.03f, 0.03f, 0.03f);
        }
        if (hv % 4 != 0) {                                   /* ceiling lamp */
            float fl = (hv % 5 == 2) ? ((g_frame % 9 < 3) ? 0.1f : 1.0f) : 1.0f;
            float p[12] = { -0.6f, CEIL_Y - 0.02f, z0 - 3.8f, 0.6f, CEIL_Y - 0.02f, z0 - 3.8f,
                             0.6f, CEIL_Y - 0.02f, z0 - 4.2f, -0.6f, CEIL_Y - 0.02f, z0 - 4.2f };
            quadU(p, pack(0.55f * fl, 0.6f * fl, 0.5f * fl));
        }
        if (hv & 1) {                                        /* stain on the floor */
            float sx = ((hv >> 3) % 40) / 10.0f - 2.0f, sz = z0 - 1.0f - ((hv >> 9) % 40) / 10.0f;
            float p[12] = { sx, FLOOR_Y + 0.01f, sz, sx + 1.4f, FLOOR_Y + 0.01f, sz,
                            sx + 1.2f, FLOOR_Y + 0.01f, sz - 2.2f, sx - 0.1f, FLOOR_Y + 0.01f, sz - 1.8f };
            quad3(p, 0.40f, 0.02f, 0.02f);
        }
    }
    /* far end of the corridor: black */
    float zf = -(NSEG * SEG_LEN) + off;
    float wall[12] = { -3, FLOOR_Y, zf, 3, FLOOR_Y, zf, 3, CEIL_Y, zf, -3, CEIL_Y, zf };
    quadU(wall, 0x000000FF);
}

/* ---------------------------------------------------------- creatures */

static void DrawEnemy(const Enemy *e) {
    float z = -e->z, x = e->x, base = FLOOR_Y;
    float r, g, b;
    float sw = sinf(e->phase) * 0.15f;
    z = e->z;                                   /* e->z is already camera-space (negative) */
    int hit = e->hit > 0;

    if (e->type == E_GAUNT) {
        r = hit ? 1 : 0.55f; g = hit ? 1 : 0.50f; b = hit ? 1 : 0.45f;
        box(x - 0.18f, base + 0.6f, z + sw, 0.14f, 1.2f, 0.14f, r, g, b);
        box(x + 0.18f, base + 0.6f, z - sw, 0.14f, 1.2f, 0.14f, r, g, b);
        box(x, base + 1.55f, z, 0.55f, 0.9f, 0.28f, r * .9f, g * .9f, b * .9f);
        box(x, base + 2.25f, z + 0.02f, 0.28f, 0.52f, 0.28f, r, g, b);
        box(x - 0.42f - sw, base + 1.15f, z + 0.1f, 0.1f, 1.6f, 0.1f, r, g, b);
        box(x + 0.42f + sw, base + 1.15f, z + 0.1f, 0.1f, 1.6f, 0.1f, r, g, b);
        float fz = z + 0.17f;
        glow(x - 0.07f, base + 2.32f, fz, 0.035f, 0.025f, 0xFF2010FF);
        glow(x + 0.07f, base + 2.32f, fz, 0.035f, 0.025f, 0xFF2010FF);
        glow(x, base + 2.13f, fz, 0.06f, 0.11f, 0x000000FF);                    /* open mouth */
    } else if (e->type == E_CRAWLER) {
        r = hit ? 1 : 0.30f; g = hit ? 1 : 0.30f; b = hit ? 1 : 0.35f;
        float j = sinf(e->phase * 2.0f) * 0.07f;
        box(x, base + 0.35f, z, 0.9f, 0.3f, 1.1f, r, g, b);
        box(x - 0.5f, base + 0.2f, z - 0.4f + j, 0.08f, 0.4f, 0.08f, r, g, b);
        box(x + 0.5f, base + 0.2f, z - 0.4f - j, 0.08f, 0.4f, 0.08f, r, g, b);
        box(x - 0.5f, base + 0.2f, z + 0.4f - j, 0.08f, 0.4f, 0.08f, r, g, b);
        box(x + 0.5f, base + 0.2f, z + 0.4f + j, 0.08f, 0.4f, 0.08f, r, g, b);
        box(x, base + 0.42f, z + 0.55f, 0.35f, 0.28f, 0.3f, r * 1.2f, g * 1.2f, b * 1.2f);
        float fz = z + 0.71f;
        glow(x - 0.1f, base + 0.48f, fz, 0.03f, 0.03f, 0xC8FF30FF);
        glow(x + 0.1f, base + 0.48f, fz, 0.03f, 0.03f, 0xC8FF30FF);
        glow(x - 0.04f, base + 0.40f, fz, 0.02f, 0.02f, 0xC8FF30FF);
        glow(x + 0.04f, base + 0.40f, fz, 0.02f, 0.02f, 0xC8FF30FF);
    } else {
        r = hit ? 1 : 0.38f; g = hit ? 1 : 0.18f; b = hit ? 1 : 0.14f;
        box(x - 0.45f, base + 0.7f, z + sw, 0.45f, 1.4f, 0.45f, r, g, b);
        box(x + 0.45f, base + 0.7f, z - sw, 0.45f, 1.4f, 0.45f, r, g, b);
        box(x, base + 2.1f, z, 1.7f, 1.5f, 0.8f, r * 1.1f, g * 1.1f, b * 1.1f);
        box(x - 1.15f - sw, base + 1.9f, z + 0.05f, 0.5f, 1.7f, 0.5f, r, g, b);
        box(x + 1.15f + sw, base + 1.9f, z + 0.05f, 0.5f, 1.7f, 0.5f, r, g, b);
        box(x, base + 3.0f, z + 0.1f, 0.6f, 0.5f, 0.5f, r * 1.2f, g * 1.2f, b * 1.2f);
        box(x, base + 2.72f, z + 0.2f, 0.55f, 0.18f, 0.4f, r * .5f, g * .5f, b * .5f);
        for (int i = 0; i < 4; i++)
            box(x - 0.2f + i * 0.13f, base + 2.84f, z + 0.38f, 0.05f, 0.1f, 0.05f, 0.9f, 0.9f, 0.8f);
        float fz = z + 0.36f;
        glow(x - 0.14f, base + 3.1f, fz, 0.05f, 0.03f, 0xFF8010FF);
        glow(x + 0.14f, base + 3.1f, fz, 0.05f, 0.03f, 0xFF8010FF);
    }
}

/* ------------------------------------------------------------ 2D prims */

static void rect(float x, float y, float w, float h, u32 c) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(x, y, -1);         GX_Color1u32(c);
    GX_Position3f32(x + w, y, -1);     GX_Color1u32(c);
    GX_Position3f32(x + w, y + h, -1); GX_Color1u32(c);
    GX_Position3f32(x, y + h, -1);     GX_Color1u32(c);
    GX_End();
}
static void tri(float x1, float y1, float x2, float y2, float x3, float y3, u32 c) {
    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GX_Position3f32(x1, y1, -1); GX_Color1u32(c);
    GX_Position3f32(x2, y2, -1); GX_Color1u32(c);
    GX_Position3f32(x3, y3, -1); GX_Color1u32(c);
    GX_End();
}
static void ell(float cx, float cy, float rx, float ry, u32 c) {
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, 18);
    GX_Position3f32(cx, cy, -1); GX_Color1u32(c);
    for (int i = 0; i < 17; i++) { GX_Position3f32(cx + ct[i] * rx, cy + st[i] * ry, -1); GX_Color1u32(c); }
    GX_End();
}

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

static const char FONT_CH[] = "ACDEGHILOPRSTUVWY";
static const u8 FONT[17][5] = {
    {2,5,7,5,5},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},
    {4,4,4,4,7},{2,5,5,5,2},{6,5,6,4,4},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},
    {5,5,5,5,2},{5,5,7,7,5},{5,5,2,2,2}
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
static void ctext(const char *s, float y, float px, u32 c) {
    text(s, 320 - (strlen(s) * px * 4 - px) / 2, y, px, c);
}

/* ------------------------------------------------------------ init */

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

    GXColor bg = { 0, 0, 0, 0xFF };
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
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_CopyDisp(xfb[fb], GX_TRUE);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetNumChans(1);
    GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
}

static void Begin3D(void) {
    Mtx44 proj;
    guPerspective(proj, 60, 640.0f / 480.0f, 0.1f, 300.0f);
    GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    Mtx mv;
    float sx = 0, sy = 0;
    if (g_shake > 0) { sx = ((rand() % 100) - 50) / 900.0f * g_shake; sy = ((rand() % 100) - 50) / 900.0f * g_shake; }
    guMtxIdentity(mv);
    guMtxTrans(mv, sx, sy, 0);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
}

static void Begin2D(void) {
    Mtx44 proj;
    guOrtho(proj, 0, 479, 0, 639, 0, 300);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    Mtx mv;
    guMtxIdentity(mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
}

/* ------------------------------------------------------------ game */

static void ResetGame(void) {
    memset(g_e, 0, sizeof(g_e));
    g_health = 100; g_ammo = MAG; g_reload = 0; g_cool = 0;
    g_score = 0; g_kills = 0; g_wave = 1; g_spawn = 90;
    g_shake = g_hurt = g_scare = g_muzzle = 0;
}

static int ActiveEnemies(void) {
    int n = 0;
    for (int i = 0; i < MAXE; i++) n += g_e[i].active;
    return n;
}

static void Spawn(void) {
    for (int i = 0; i < MAXE; i++) {
        if (g_e[i].active) continue;
        Enemy *e = &g_e[i];
        memset(e, 0, sizeof(*e));
        e->active = 1;
        int r = rand() % 100;
        e->type = (g_wave >= 2 && r < 12) ? E_BRUTE : (r < 62 ? E_GAUNT : E_CRAWLER);
        e->x = ((rand() % 460) - 230) / 100.0f;
        e->z = -(38.0f + rand() % 10);
        e->phase = (rand() % 628) / 100.0f;
        switch (e->type) {
            case E_GAUNT:   e->hp = 3;  e->speed = 2.0f + g_wave * 0.12f; break;
            case E_CRAWLER: e->hp = 1;  e->speed = 4.0f + g_wave * 0.18f; break;
            default:        e->hp = 10; e->speed = 1.2f + g_wave * 0.08f; break;
        }
        return;
    }
}

static void Shoot(void) {
    if (g_reload > 0 || g_cool > 0) return;
    if (g_ammo <= 0) { play(4, sn_click, n_click, 200); g_cool = 12; return; }
    g_ammo--; g_cool = 9; g_shake = 6; g_muzzle = 3;
    play(1, sn_shot, n_shot, 255);

    int best = -1, head = 0; float bestz = -9999;
    for (int i = 0; i < MAXE; i++) {
        Enemy *e = &g_e[i];
        if (!e->active || e->z > -0.5f) continue;
        float hy, hr, by, br;
        switch (e->type) {
            case E_GAUNT:   hy = FLOOR_Y + 2.25f; hr = 0.32f; by = FLOOR_Y + 1.3f; br = 0.65f; break;
            case E_CRAWLER: hy = FLOOR_Y + 0.42f; hr = 0.28f; by = FLOOR_Y + 0.35f; br = 0.60f; break;
            default:        hy = FLOOR_Y + 3.0f;  hr = 0.50f; by = FLOOR_Y + 2.0f;  br = 1.10f; break;
        }
        float inv = F_LEN / (-e->z);
        float sx = 320 + e->x * inv;
        float dxh = g_x - sx, dyh = g_y - (240 - hy * inv);
        float dyb = g_y - (240 - by * inv);
        int ishead = (dxh * dxh + dyh * dyh) <= (hr * inv) * (hr * inv);
        int isbody = (dxh * dxh + dyb * dyb) <= (br * inv) * (br * inv);
        if ((ishead || isbody) && e->z > bestz) { best = i; head = ishead; bestz = e->z; }
    }
    if (best >= 0) {
        Enemy *e = &g_e[best];
        e->hit = 5;
        e->hp -= head ? 3 : 1;
        e->z -= 0.25f;                                          /* knock back */
        if (e->hp <= 0) {
            e->active = 0;
            g_score += (e->type == E_BRUTE ? 500 : (e->type == E_GAUNT ? 150 : 100)) + (head ? 50 : 0);
            if (g_score > 999999) g_score = 999999;
            if (++g_kills % 10 == 0) g_wave++;
        }
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
        if (held & WPAD_BUTTON_LEFT)  g_x -= 8;
        if (held & WPAD_BUTTON_RIGHT) g_x += 8;
        if (held & WPAD_BUTTON_UP)    g_y -= 8;
        if (held & WPAD_BUTTON_DOWN)  g_y += 8;
    }
    g_x = clampf(g_x, 0, 639);
    g_y = clampf(g_y, 0, 479);

    float dx = (g_x - 320) / F_LEN, dy = -(g_y - 240) / F_LEN;
    float l = sqrtf(dx * dx + dy * dy + 1.0f);
    g_ax = dx / l; g_ay = dy / l; g_az = -1.0f / l;

    /* atmosphere */
    g_pulse = 0.5f + 0.5f * sinf(g_frame * 0.05f);
    g_flick = 1.0f;
    if ((g_frame % 211) < 4 || (g_frame % 347) < 2) g_flick = 0.15f + (rand() % 40) / 100.0f;
    g_beamf = ((g_frame % 173) < 3) ? 0.2f + (rand() % 50) / 100.0f : 1.0f;
    g_frame++;

    if (g_mode != ST_PLAY) {
        g_travel += 0.05f;
        if (pressed & (WPAD_BUTTON_A | WPAD_BUTTON_B | WPAD_BUTTON_PLUS)) { ResetGame(); g_mode = ST_PLAY; }
        return 1;
    }

    if (g_cool > 0) g_cool--;
    if (g_shake > 0) g_shake--;
    if (g_muzzle > 0) g_muzzle--;
    if (g_hurt > 0) g_hurt--;
    if (g_scare > 0) g_scare--;

    if (pressed & WPAD_BUTTON_B) Shoot();
    if ((pressed & WPAD_BUTTON_A) && g_reload == 0 && g_ammo < MAG) { g_reload = RELOAD_F; play(4, sn_click, n_click, 220); }
    if (g_reload > 0 && --g_reload == 0) { g_ammo = MAG; play(4, sn_click, n_click, 220); }

    int nE = ActiveEnemies();
    g_travel += nE ? 0.02f : 0.07f;

    int maxE = 2 + g_wave; if (maxE > 8) maxE = 8;
    int interval = 110 - g_wave * 8; if (interval < 30) interval = 30;
    if (--g_spawn <= 0) {
        if (nE < maxE) Spawn();
        g_spawn = interval / 2 + rand() % interval;
    }

    for (int i = 0; i < MAXE; i++) {
        Enemy *e = &g_e[i];
        if (!e->active) continue;
        if (e->hit > 0) e->hit--;
        e->phase += 0.15f + e->speed * 0.02f;
        e->z += e->speed / 60.0f;
        if (e->type == E_CRAWLER) e->x += sinf(e->phase * 1.7f) * 0.03f;
        else                      e->x += sinf(e->phase * 0.6f) * 0.006f;
        e->x = clampf(e->x, -2.4f, 2.4f);
        if (e->z > -1.3f) {                                      /* it got you */
            g_health -= (e->type == E_BRUTE) ? 40 : (e->type == E_GAUNT ? 20 : 10);
            e->active = 0;
            g_scare = 14; g_hurt = 18; g_shake = 12;
            play(2, sn_scare, n_scare, 255);
            if (g_health <= 0) {
                g_health = 0;
                if (g_score > g_best) g_best = g_score;
                g_mode = ST_OVER;
            }
        }
    }

    if (g_health > 0 && g_health < 40 && (g_frame % 50) == 0) play(3, sn_heart, n_heart, 255);
    return 1;
}

/* ------------------------------------------------------------ draw */

static void DrawJumpScare(void) {
    rect(0, 0, 640, 480, 0x30000090);
    float s = 1.0f + (14 - g_scare) * 0.02f;
    float ex = 120 * s;
    ell(320 - ex, 190, 62 * s, 36 * s, 0xFF3010FF);
    ell(320 + ex, 190, 62 * s, 36 * s, 0xFF3010FF);
    ell(320 - ex, 190, 9 * s, 34 * s, 0x000000FF);
    ell(320 + ex, 190, 9 * s, 34 * s, 0x000000FF);
    ell(320, 365, 190 * s, 95 * s, 0x000000FF);
    for (int i = 0; i < 9; i++) {
        float x = 320 - 160 * s + i * 40 * s;
        tri(x, 292 + 6, x + 40 * s, 292 + 6, x + 20 * s, 345, 0xE8E0C0FF);
        tri(x, 438, x + 40 * s, 438, x + 20 * s, 385, 0xE8E0C0FF);
    }
}

static void DrawGun(void) {
    float kx = (g_muzzle > 0) ? -3 : 0, ky = (g_muzzle > 0) ? 4 : 0;
    float bx = 430 + kx, by = 360 + ky;
    rect(bx, by, 210, 36, 0x202226FF);                    /* slide */
    rect(bx + 20, by + 36, 150, 14, 0x16181AFF);
    rect(bx + 150, by + 36, 56, 120, 0x181A1CFF);         /* grip  */
    rect(bx - 40, by + 8, 50, 14, 0x101214FF);            /* barrel */
    rect(bx + 10, by + 4, 190, 4, 0x3A3E44FF);
    rect(bx + 66, by + 36, 4, 40, 0x101214FF);            /* trigger guard */
    if (g_muzzle > 0) {
        ell(bx - 54, by + 15, 34, 20, 0xFFD060FF);
        ell(bx - 54, by + 15, 18, 10, 0xFFFFE0FF);
    }
}

static void DrawHud(void) {
    number(g_score, 6, 20, 16, 30, 0xC8C8C8D0);
    text("WAVE", 470, 18, 4, 0x909090FF);
    number(g_wave, 2, 560, 14, 30, 0xC8C8C8D0);
    rect(20, 440, 190, 16, 0x101010D0);                                      /* health */
    rect(22, 442, 186.0f * g_health / 100.0f, 12, g_health > 35 ? 0x8A1010FF : (0xFF2020FF));
    for (int i = 0; i < MAG; i++)                                            /* ammo   */
        rect(240 + i * 14, 436, 9, 24, i < g_ammo ? 0xD0C890FF : 0x302E28FF);
    if (g_reload > 0) rect(240, 466, 112.0f * (RELOAD_F - g_reload) / RELOAD_F, 4, 0xE0E0E0FF);

    u32 c = 0xFFFFFFC0;                                                      /* crosshair */
    ell(g_x, g_y, 3, 3, c);
    rect(g_x - 16, g_y - 1, 8, 2, c);
    rect(g_x + 8,  g_y - 1, 8, 2, c);
    rect(g_x - 1, g_y - 16, 2, 8, c);
    rect(g_x - 1, g_y + 8,  2, 8, c);
}

static void DrawVignette(void) {
    for (int i = 0; i < 6; i++) {
        u32 a = (u32)(0x58 - i * 0x0E);
        u32 c = 0x00000000 | a;
        rect(0, i * 14, 640, 14, c);
        rect(0, 466 - i * 14, 640, 14, c);
        rect(i * 18, 0, 18, 480, c);
        rect(622 - i * 18, 0, 18, 480, c);
    }
    if (g_hurt > 0) rect(0, 0, 640, 480, 0xB0000000 | (u32)(g_hurt * 4));
    if (g_health > 0 && g_health < 40) {
        u32 a = 0x20 + (u32)(0x20 * g_pulse);
        rect(0, 0, 640, 12, 0xA0000000 | a); rect(0, 468, 640, 12, 0xA0000000 | a);
    }
}

static void Draw(void) {
    Begin3D();
    DrawWorld();
    for (int i = 0; i < MAXE; i++)
        if (g_e[i].active && g_mode == ST_PLAY) DrawEnemy(&g_e[i]);

    Begin2D();
    if (g_mode == ST_TITLE) {
        DrawVignette();
        float fl = ((g_frame % 97) < 4) ? 0.3f : 1.0f;
        u32 tc = pack(0.85f * fl, 0.04f * fl, 0.04f * fl);
        ctext("DEAD", 70, 18, tc);
        ctext("LIGHT", 175, 18, tc);
        if ((g_frame / 40) & 1) ctext("PRESS A", 380, 6, 0xB0B0B0FF);
        if (g_best > 0) { text("HI", 24, 436, 5, 0x909090FF); number(g_best, 6, 70, 430, 30, 0xB0B0B0FF); }
        ell(g_x, g_y, 4, 4, 0xFFFFFFC0);
        return;
    }

    if (g_mode == ST_PLAY) DrawGun();
    DrawVignette();
    if (g_scare > 0) DrawJumpScare();
    DrawHud();

    if (g_mode == ST_OVER) {
        rect(0, 0, 640, 480, 0x000000B8);
        ctext("YOU DIED", 130, 14, 0xC01010FF);
        text("SCORE", 190, 270, 6, 0xB0B0B0FF);
        number(g_score, 6, 310, 262, 38, 0xE0E0E0FF);
        if ((g_frame / 40) & 1) ctext("PRESS A", 380, 5, 0xB0B0B0FF);
    }
}

int main(int argc, char **argv) {
    InitVideo();
    InitGX();
    InitSound();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_0, 640, 480);
    srand(0xDEAD11);
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
    }

    ASND_End();
    WPAD_Shutdown();
    return 0;
}
