/*
 * CAR DRIVING - PSP port
 * Game by Cavero and Christian
 * Layar 480x272 (resolusi asli PSP), target 30 FPS.
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psppower.h>
#include <psprtc.h>
#include <psputils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <stdint.h>

PSP_MODULE_INFO("CarDriving", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

typedef unsigned int u32;
#define PI 3.14159265f
#define DT (1.0f/30.0f)
#define BUF_W 512
#define SCR_W 480
#define SCR_H 272

#define RGB(r,g,b) (0xFF000000u|((u32)(b)<<16)|((u32)(g)<<8)|(u32)(r))
#define RGBA(r,g,b,a) (((u32)(a)<<24)|((u32)(b)<<16)|((u32)(g)<<8)|(u32)(r))
#define HEX(h) RGB(((h)>>16)&255,((h)>>8)&255,(h)&255)

typedef struct { u32 c; float x, y, z; } Vtx;
#define VF3 (GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D)
#define VF2 (GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D)

static unsigned int __attribute__((aligned(16))) dlist[262144];

/* ------------------------------------------------------------ util */
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float minf(float a, float b) { return a < b ? a : b; }
static float rnd(void) { return (float)rand() / (float)RAND_MAX; }
static u32 shade(u32 c, float k) {
    int r = (int)((c & 255) * k), g = (int)(((c >> 8) & 255) * k), b = (int)(((c >> 16) & 255) * k);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return RGB(r, g, b);
}
static u32 mixw(u32 c, float k) {
    int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
    r += (int)((255 - r) * k); g += (int)((255 - g) * k); b += (int)((255 - b) * k);
    return RGB(r, g, b);
}
static u32 hsvCol(float h) {
    float r, g, b; h -= floorf(h); float f = h * 6; int i = (int)f; float q = f - i;
    float s = .85f, v = .95f, p = v * (1 - s), qq = v * (1 - s * q), t = v * (1 - s * (1 - q));
    switch (i % 6) { case 0: r = v; g = t; b = p; break; case 1: r = qq; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break; case 3: r = p; g = qq; b = v; break;
        case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = qq; break; }
    return RGB((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

/* ------------------------------------------------------------ bahasa */
static int LANG = 0; /* 0 Indonesia, 1 English */
#define TR(id, en) (LANG ? (en) : (id))

/* ------------------------------------------------------------ mesh cache */
enum { M_CUBE, M_CYL, M_CONE8, M_PYR4 };
typedef struct { int type; u32 col; Vtx *v; int n; } MeshE;
static MeshE mc[1024];

static void pv(Vtx *v, int *k, u32 c, float x, float y, float z) {
    v[*k].c = c; v[*k].x = x; v[*k].y = y; v[*k].z = z; (*k)++;
}
static const float CF[6][4][3] = {
    {{-.5f,.5f,-.5f},{-.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,.5f,-.5f}},
    {{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,-.5f,.5f},{-.5f,-.5f,.5f}},
    {{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}},
    {{-.5f,-.5f,-.5f},{-.5f,.5f,-.5f},{.5f,.5f,-.5f},{.5f,-.5f,-.5f}},
    {{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{.5f,.5f,.5f},{.5f,-.5f,.5f}},
    {{-.5f,-.5f,-.5f},{-.5f,-.5f,.5f},{-.5f,.5f,.5f},{-.5f,.5f,-.5f}}};
static const float CSH[6] = {1.0f, .45f, .85f, .7f, .92f, .6f};
static const int QI[6] = {0, 1, 2, 0, 2, 3};

static MeshE *meshFor(int type, u32 col) {
    unsigned h = (((col * 2654435761u) >> 22) + type * 37) & 1023;
    while (mc[h].v) { if (mc[h].col == col && mc[h].type == type) return &mc[h]; h = (h + 1) & 1023; }
    int n = 0, cap = 128, k = 0;
    if (type == M_CUBE) cap = 36; else if (type == M_CYL) cap = 120; else if (type == M_CONE8) cap = 48; else cap = 24;
    Vtx *v = (Vtx *)memalign(16, cap * sizeof(Vtx));
    if (type == M_CUBE) {
        for (int f = 0; f < 6; f++) { u32 c = shade(col, CSH[f]);
            for (int i = 0; i < 6; i++) pv(v, &k, c, CF[f][QI[i]][0], CF[f][QI[i]][1], CF[f][QI[i]][2]); }
    } else if (type == M_CYL) {
        const int seg = 10;
        for (int i = 0; i < seg; i++) {
            float a0 = i * 2 * PI / seg, a1 = (i + 1) * 2 * PI / seg;
            float x0 = .5f * cosf(a0), z0 = .5f * sinf(a0), x1 = .5f * cosf(a1), z1 = .5f * sinf(a1);
            u32 cs = shade(col, .65f + .35f * cosf((a0 + a1) * .5f));
            pv(v, &k, cs, x0, -.5f, z0); pv(v, &k, cs, x1, -.5f, z1); pv(v, &k, cs, x1, .5f, z1);
            pv(v, &k, cs, x0, -.5f, z0); pv(v, &k, cs, x1, .5f, z1); pv(v, &k, cs, x0, .5f, z0);
            pv(v, &k, col, 0, .5f, 0); pv(v, &k, col, x0, .5f, z0); pv(v, &k, col, x1, .5f, z1);
            u32 cb = shade(col, .5f);
            pv(v, &k, cb, 0, -.5f, 0); pv(v, &k, cb, x1, -.5f, z1); pv(v, &k, cb, x0, -.5f, z0);
        }
    } else {
        int seg = (type == M_CONE8) ? 8 : 4;
        float r = (type == M_CONE8) ? .5f : .70711f, rot = (type == M_CONE8) ? 0 : PI / 4;
        for (int i = 0; i < seg; i++) {
            float a0 = rot + i * 2 * PI / seg, a1 = rot + (i + 1) * 2 * PI / seg;
            float x0 = r * cosf(a0), z0 = r * sinf(a0), x1 = r * cosf(a1), z1 = r * sinf(a1);
            u32 cs = shade(col, .7f + .3f * cosf((a0 + a1) * .5f));
            pv(v, &k, cs, x0, 0, z0); pv(v, &k, cs, x1, 0, z1); pv(v, &k, cs, 0, 1, 0);
            u32 cb = shade(col, .5f);
            pv(v, &k, cb, 0, 0, 0); pv(v, &k, cb, x1, 0, z1); pv(v, &k, cb, x0, 0, z0);
        }
    }
    n = k;
    sceKernelDcacheWritebackRange(v, cap * sizeof(Vtx));
    mc[h].type = type; mc[h].col = col; mc[h].v = v; mc[h].n = n;
    return &mc[h];
}

static void dm(int type, u32 col, float x, float y, float z, float sx, float sy, float sz,
               float ry, float rx, float rz) {
    MeshE *m = meshFor(type, col);
    ScePspFVector3 t = {x, y, z}, s = {sx, sy, sz};
    sceGumPushMatrix();
    sceGumTranslate(&t);
    if (ry != 0) sceGumRotateY(ry);
    if (rx != 0) sceGumRotateX(rx);
    if (rz != 0) sceGumRotateZ(rz);
    sceGumScale(&s);
    sceGumDrawArray(GU_TRIANGLES, VF3, m->n, 0, m->v);
    sceGumPopMatrix();
}
#define BX(c,x,y,z,w,h,l) dm(M_CUBE,(c),(x),(y),(z),(w),(h),(l),0,0,0)

/* ------------------------------------------------------------ 2D / teks */
static void rect(float x, float y, float w, float h, u32 c) {
    Vtx *v = (Vtx *)sceGuGetMemory(2 * sizeof(Vtx));
    v[0].c = c; v[0].x = x; v[0].y = y; v[0].z = 0;
    v[1].c = c; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, VF2, 2, 0, v);
}
static void line2(float x0, float y0, float x1, float y1, u32 c) {
    Vtx *v = (Vtx *)sceGuGetMemory(2 * sizeof(Vtx));
    v[0].c = c; v[0].x = x0; v[0].y = y0; v[0].z = 0;
    v[1].c = c; v[1].x = x1; v[1].y = y1; v[1].z = 0;
    sceGuDrawArray(GU_LINES, VF2, 2, 0, v);
}
static void tri2(float x0, float y0, float x1, float y1, float x2, float y2, u32 c) {
    Vtx *v = (Vtx *)sceGuGetMemory(3 * sizeof(Vtx));
    v[0].c = c; v[0].x = x0; v[0].y = y0; v[0].z = 0;
    v[1].c = c; v[1].x = x1; v[1].y = y1; v[1].z = 0;
    v[2].c = c; v[2].x = x2; v[2].y = y2; v[2].z = 0;
    sceGuDrawArray(GU_TRIANGLES, VF2, 3, 0, v);
}
static const char FC[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,:-+/!?%()=<>*_";
static const unsigned char FG[][7] = {
    {0,0,0,0,0,0,0},
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x19,0x15,0x13,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0,0,0,0,0,0x0C,0x0C},{0,0,0,0,0x0C,0x04,0x08},{0,0x0C,0x0C,0,0x0C,0x0C,0},
    {0,0,0,0x1F,0,0,0},{0,0x04,0x04,0x1F,0x04,0x04,0},{0x01,0x01,0x02,0x04,0x08,0x10,0x10},
    {0x04,0x04,0x04,0x04,0x04,0,0x04},{0x0E,0x11,0x01,0x02,0x04,0,0x04},
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03},{0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08},{0,0,0x1F,0,0x1F,0,0},
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02},{0x08,0x04,0x02,0x01,0x02,0x04,0x08},
    {0,0x0A,0x04,0x1F,0x04,0x0A,0},{0,0,0,0,0,0,0x1F}};

static void text(float x, float y, const char *s, float sc, u32 col) {
    for (; *s; s++) {
        char ch = *s; if (ch >= 'a' && ch <= 'z') ch -= 32;
        const char *p = strchr(FC, ch);
        if (p && ch != ' ') {
            const unsigned char *g = FG[p - FC];
            for (int r = 0; r < 7; r++) {
                int b = g[r], c = 0;
                while (c < 5) {
                    if (b & (16 >> c)) { int e = c; while (e < 5 && (b & (16 >> e))) e++;
                        rect(x + c * sc, y + r * sc, (e - c) * sc, sc, col); c = e; }
                    else c++;
                }
            }
        }
        x += 6 * sc;
    }
}
static float textw(const char *s, float sc) { return strlen(s) * 6 * sc; }
static void texts(float x, float y, const char *s, float sc, u32 col) {
    text(x + sc * .7f, y + sc * .7f, s, sc, RGBA(0, 0, 0, 200)); text(x, y, s, sc, col);
}
static void textc(float cx, float y, const char *s, float sc, u32 col) { texts(cx - textw(s, sc) / 2, y, s, sc, col); }
static void panel(float x, float y, float w, float h, u32 c) {
    rect(x, y, w, h, c); rect(x, y, w, 1, RGBA(255, 255, 255, 90)); rect(x, y + h - 1, w, 1, RGBA(255, 255, 255, 40));
}
static void barr(float x, float y, float w, float h, float f, u32 c) {
    rect(x, y, w, h, RGB(43, 53, 82)); rect(x, y, w * clampf(f, 0, 1), h, c);
}
static void fmtM(int n, char *o) {
    char t[24]; int neg = n < 0; if (neg) n = -n; snprintf(t, 24, "%d", n);
    int l = (int)strlen(t), k = 0; o[k++] = 'R'; o[k++] = 'P'; o[k++] = ' ';
    for (int i = 0; i < l; i++) { if (i && (l - i) % 3 == 0) o[k++] = '.'; o[k++] = t[i]; }
    o[k] = 0;
}

/* ------------------------------------------------------------ data mobil */
enum { F_SPOIL = 1, F_GTWING = 2, F_RACK = 4, F_BED = 8, F_CANARD = 16, F_STRIPE = 32, F_DIFF = 64,
       F_HAUNCH = 128, F_BAT = 256, F_RAINBOW = 512, F_EXHAUST = 1024, F_WEDGE = 2048, F_SCOOP = 4096,
       F_INTAKE = 8192, F_ROUND = 16384, F_STRIPE2 = 32768 };
typedef struct {
    const char *n, *t; int price, lim, secret; u32 col[3];
    float w, h, l, cw, ch, cl, cz, wr, wrr; float spd, acc, hnd; unsigned fl;
} CarDef;
#define NCARS 13
static const CarDef CARS[NCARS] = {
 {"Mungil City","Hatchback",0,0,0,{0xf06292,0x4fc3f7,0x80cbc4},1.7f,.75f,3.3f,1.5f,.7f,2.0f,-.1f,.4f,0,22,15,2.8f,0},
 {"Retro Classic","Classic",100000,0,0,{0x00897b,0xfff3c4,0x8d1f2b},1.9f,.7f,4.4f,1.55f,.65f,1.9f,-.2f,.5f,0,28,12,2.3f,0},
 {"Kuli Pickup","Pickup",200000,0,0,{0xfdd835,0xd32f2f,0xeeeeee},2.0f,.85f,4.8f,1.8f,.7f,1.5f,.9f,.55f,0,26,11,2.1f,F_BED},
 {"Rimba 4x4","Off-road SUV",300000,0,0,{0x43a047,0xfb8c00,0x757575},2.0f,.95f,4.2f,1.8f,.8f,2.8f,-.3f,.62f,0,24,12,2.4f,F_RACK},
 {"Muscle V8","Muscle Car",400000,0,0,{0x212121,0xff6f00,0x1565c0},2.05f,.62f,4.7f,1.6f,.45f,1.8f,-.35f,.47f,0,36,17,1.8f,F_STRIPE|F_STRIPE2|F_SCOOP|F_EXHAUST},
 {"Turbo GT","Sport Coupe",500000,0,0,{0xe53935,0x1e88e5,0xfdd835},1.9f,.6f,4.3f,1.55f,.5f,2.2f,-.2f,.45f,0,34,14,2.2f,F_SPOIL},
 {"Hyper X","Supercar",650000,0,0,{0x8e24aa,0x00bcd4,0xf5f5f5},2.1f,.48f,4.6f,1.5f,.4f,2.0f,-.3f,.42f,0,40,16,1.9f,F_SPOIL|F_DIFF},
 {"Storm Hyper","Hypercar",800000,0,0,{0x1565c0,0x90a4ae,0x111111},2.1f,.46f,4.7f,1.45f,.38f,1.9f,-.3f,.43f,0,44,18,2.0f,F_CANARD|F_HAUNCH|F_GTWING|F_DIFF|F_INTAKE},
 {"Ferrari","Grand Tourer V8",1000000,0,0,{0xd50000,0xffd600,0x212121},2.0f,.55f,4.5f,1.45f,.42f,2.0f,-.2f,.44f,0,41,18,2.0f,F_HAUNCH|F_ROUND|F_EXHAUST|F_INTAKE},
 {"Cavero R1","Hypercar",1250000,0,0,{0xd50000,0x1565c0,0xf5f5f5},2.12f,.46f,4.7f,1.5f,.38f,1.9f,-.3f,.43f,0,46,19,2.2f,F_STRIPE|F_CANARD|F_SCOOP|F_GTWING|F_DIFF|F_INTAKE|F_ROUND},
 {"Lamborghini","Supercar V12",1500000,0,0,{0xffd600,0xff6d00,0x00c853},2.15f,.5f,4.6f,1.55f,.4f,2.0f,-.25f,.44f,0,42,17,1.8f,F_SPOIL|F_WEDGE|F_INTAKE|F_DIFF},
 {"Batman Limited","Limited Edition",1900000,1,0,{0x0c0c0e,0x1c2330,0x2a1030},2.1f,.62f,5.2f,1.4f,.4f,1.3f,-1.25f,.5f,.68f,48,20,2.2f,F_BAT},
 {"Rainbow","Limited Edition",2000000,1,1,{0xff0000,0xff0000,0xff0000},2.12f,.46f,4.7f,1.5f,.38f,1.9f,-.3f,.43f,0,50,21,2.3f,F_CANARD|F_HAUNCH|F_GTWING|F_DIFF|F_INTAKE|F_ROUND|F_RAINBOW}
};
#define BATI 11
#define RBWI 12

static void drawCar(const CarDef *d, u32 pc, float x, float y, float z, float h, float steer,
                    float spin, float rz, float rx, int brake, float sc) {
    ScePspFVector3 t = {x, y, z};
    sceGumPushMatrix();
    sceGumTranslate(&t);
    sceGumRotateY(h);
    if (rz != 0) sceGumRotateZ(rz);
    if (rx != 0) sceGumRotateX(rx);
    if (sc != 1.0f) { ScePspFVector3 s = {sc, sc, sc}; sceGumScale(&s); }
    const u32 dark = HEX(0x222222), glass = HEX(0x2a3f5a);
    float W = d->w, H = d->h, L = d->l, yb = d->wr * .8f, top = yb + H;
    unsigned fl = d->fl;
    if (H <= .7f) {
        BX(pc, 0, yb + H * .3f, 0, W, H * .6f, L);
        BX(pc, 0, yb + H * .8f, -L * .08f, W * .95f, H * .4f, L * .62f);
    } else BX(pc, 0, yb + H / 2, 0, W, H, L);
    BX(glass, 0, top + d->ch / 2 - .03f, d->cz, d->cw, d->ch, d->cl);
    BX(pc, 0, top + d->ch, d->cz, d->cw + .04f, .07f, d->cl * .7f);
    for (int s = -1; s <= 1; s += 2) {
        BX(HEX(0xfff8c4), s * W * .3f, yb + H * .6f, L / 2 + .02f, .45f, .14f, .08f);
        BX(brake ? HEX(0xff4040) : HEX(0xaa1111), s * W * .3f, yb + H * .6f, -L / 2 - .02f, .42f, .14f, .08f);
    }
    BX(dark, 0, yb + H * .3f, L / 2 + .03f, W * .55f, .14f, .06f);
    if (fl & F_SPOIL) {
        BX(pc, 0, top + .32f, -L / 2 + .2f, W * .9f, .08f, .5f);
        BX(dark, -W * .3f, top + .15f, -L / 2 + .2f, .1f, .35f, .1f); BX(dark, W * .3f, top + .15f, -L / 2 + .2f, .1f, .35f, .1f);
    }
    if (fl & F_GTWING) {
        BX(dark, 0, top + .55f, -L / 2 + .35f, W * .95f, .05f, .55f);
        for (int s = -1; s <= 1; s += 2) { BX(pc, s * W * .47f, top + .48f, -L / 2 + .35f, .04f, .3f, .6f);
            BX(dark, s * W * .2f, top + .32f, -L / 2 + .35f, .06f, .45f, .1f); }
    }
    if (fl & F_RACK) {
        BX(dark, 0, top + d->ch + .2f, d->cz, d->cw - .2f, .06f, d->cl - .3f);
        BX(dark, -d->cw / 2 + .2f, top + d->ch + .12f, d->cz, .06f, .14f, d->cl - .3f);
        BX(dark, d->cw / 2 - .2f, top + d->ch + .12f, d->cz, .06f, .14f, d->cl - .3f);
    }
    if (fl & F_BED) {
        BX(dark, 0, top - .05f, -L / 2 + 1.1f, W - .1f, .08f, 1.9f);
        BX(pc, -W / 2 + .05f, top + .15f, -L / 2 + 1.1f, .1f, .4f, 1.9f);
        BX(pc, W / 2 - .05f, top + .15f, -L / 2 + 1.1f, .1f, .4f, 1.9f);
        BX(pc, 0, top + .15f, -L / 2 + .15f, W, .4f, .1f);
    }
    if (fl & F_CANARD) for (int s = -1; s <= 1; s += 2)
        dm(M_CUBE, dark, s * (W / 2 - .25f), yb + .12f, L / 2 - .15f, .35f, .03f, .3f, 0, 0, s * .25f);
    if (fl & F_STRIPE) {
        u32 c0, c1;
        if (fl & F_STRIPE2) { c0 = c1 = HEX(0x111111); }
        else { c0 = (pc == HEX(0xd50000)) ? HEX(0x1565c0) : HEX(0xd50000); c1 = HEX(0xffffff); }
        float zf = d->cz + d->cl / 2, hl = L / 2 - zf;
        for (int i = 0; i < 2; i++) { u32 c = i ? c1 : c0; float xx = i ? .13f : -.13f;
            BX(c, xx, yb + H + .015f, zf + hl / 2, .16f, .02f, hl);
            BX(c, xx, top + d->ch + .05f, d->cz, .16f, .02f, d->cl * .7f);
            BX(c, xx, yb + H + .015f, (-L / 2 + d->cz - d->cl / 2) / 2, .16f, .02f, d->cz - d->cl / 2 + L / 2); }
    }
    if (fl & F_SCOOP) BX(dark, 0, yb + H + .06f, L * .3f, .55f, .09f, .5f);
    if (fl & F_DIFF) BX(dark, 0, d->wr * .8f + .1f, -L / 2, W * .8f, .12f, .3f);
    if (fl & F_INTAKE) for (int s = -1; s <= 1; s += 2) BX(dark, s * (W / 2 + .02f), yb + H * .5f, -.3f, .1f, H * .4f, .9f);
    if (fl & F_HAUNCH) for (int s = -1; s <= 1; s += 2) BX(pc, s * (W / 2 - .08f), yb + H * .5f + .12f, -.75f, .28f, .3f, 1.5f);
    if (fl & F_WEDGE) BX(dark, 0, yb + .02f, L / 2 - .05f, W, .05f, .5f);
    if (fl & F_EXHAUST) for (int s = -1; s <= 1; s += 2)
        dm(M_CYL, HEX(0xcccccc), s * .25f, d->wr * .8f + .12f, -L / 2 - .12f, .14f, .2f, .14f, 0, PI / 2, 0);
    if (fl & F_ROUND) for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 2; k++)
        BX(HEX(0xff1111), s * W * (k ? .55f : .28f) * .9f, yb + H * .5f, -L / 2 - .1f, .16f, .16f, .1f);
    if (fl & F_BAT) {
        for (int s = -1; s <= 1; s += 2) {
            BX(pc, s * (W / 2 - .05f), 1.08f, -L * .31f, .62f, .6f, 1.7f);
            dm(M_CUBE, pc, s * .6f, top + .38f, -L / 2 + .85f, .07f, .95f, 1.5f, 0, .28f, 0);
            for (int k = 0; k < 3; k++) BX(HEX(0xff2020), s * (W / 2 + .27f), .85f + k * .14f, -.95f, .04f, .1f, .06f);
        }
        dm(M_CYL, dark, 0, .8f, -L / 2 - .3f, .66f, .8f, .66f, 0, PI / 2, 0);
        BX(dark, 0, yb + .02f, L / 2 - .3f, W * .85f, .04f, .7f);
    }
    /* roda */
    for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2) {
        float WR = (sz < 0 && d->wrr > 0) ? d->wrr : d->wr;
        sceGumPushMatrix();
        ScePspFVector3 wp = {sx * (W / 2), WR, sz * L * .31f};
        sceGumTranslate(&wp);
        if (sz > 0 && steer != 0) sceGumRotateY(steer * .5f);
        sceGumRotateX(spin);
        dm(M_CYL, HEX(0x111111), 0, 0, 0, WR * 2, .42f, WR * 2, 0, 0, PI / 2);
        dm(M_CYL, HEX(0xcccccc), sx * .02f, 0, 0, WR * 1.2f, .46f, WR * 1.2f, 0, 0, PI / 2);
        BX(HEX(0x777777), sx * .24f, 0, 0, .04f, WR * 1.1f, .09f);
        BX(HEX(0x777777), sx * .24f, 0, 0, .04f, .09f, WR * 1.1f);
        sceGumPopMatrix();
    }
    sceGumPopMatrix();
}

static void drawBike(u32 col, float x, float z, float h, float spin, int rider) {
    ScePspFVector3 t = {x, .08f, z};
    sceGumPushMatrix(); sceGumTranslate(&t); sceGumRotateY(h);
    for (int s = -1; s <= 1; s += 2) {
        sceGumPushMatrix(); ScePspFVector3 wp = {0, .34f, s * .8f}; sceGumTranslate(&wp); sceGumRotateX(spin);
        dm(M_CYL, HEX(0x111111), 0, 0, 0, .68f, .16f, .68f, 0, 0, PI / 2);
        BX(HEX(0x888888), 0, 0, 0, .1f, .5f, .05f);
        sceGumPopMatrix();
    }
    BX(col, 0, .62f, -.05f, .3f, .35f, .9f);
    BX(HEX(0x222222), 0, .78f, -.4f, .28f, .1f, .6f);
    BX(col, 0, .84f, -.72f, .26f, .14f, .42f);
    BX(HEX(0xcccccc), 0, 1.0f, .52f, .7f, .05f, .05f);
    BX(HEX(0xfff4c2), 0, .88f, .82f, .2f, .2f, .12f);
    if (rider) {
        u32 sc = HEX(0x263238);
        BX(sc, 0, 1.22f, -.22f, .34f, .5f, .26f);
        BX(col, 0, 1.6f, -.02f, .3f, .3f, .3f);
        BX(sc, .22f, 1.0f, .15f, .1f, .5f, .1f); BX(sc, -.22f, 1.0f, .15f, .1f, .5f, .1f);
        BX(sc, .2f, .8f, -.3f, .13f, .5f, .13f); BX(sc, -.2f, .8f, -.3f, .13f, .5f, .13f);
    }
    sceGumPopMatrix();
}
/* ------------------------------------------------------------ save */
typedef struct {
    unsigned magic; int money, best, mcoins, mearn, mrun, mspd, mbat, lastSpin, lang, weather;
    unsigned char owned[NCARS], lv[NCARS], done[5]; char used[8][16];
} Save;
static Save S;
static char savePath[300] = "ms0:/PSP/GAME/CarDriving/save.dat";
static void saveGame(void) { S.magic = 0xCA2D0001u; FILE *f = fopen(savePath, "wb"); if (f) { fwrite(&S, sizeof(S), 1, f); fclose(f); } }
static void loadGame(void) {
    memset(&S, 0, sizeof(S)); S.owned[0] = 1;
    for (int i = 0; i < NCARS; i++) S.lv[i] = 1;
    FILE *f = fopen(savePath, "rb");
    if (f) { Save t; if (fread(&t, sizeof(t), 1, f) == 1 && t.magic == 0xCA2D0001u) S = t; fclose(f); }
    for (int i = 0; i < NCARS; i++) if (S.lv[i] < 1) S.lv[i] = 1;
    S.owned[0] = 1; LANG = S.lang ? 1 : 0;
}
static void hashCode(const char *s, char *out) {
    uint32_t h1 = 0xdeadbeefu, h2 = 0x41c6ce57u;
    for (; *s; s++) { uint32_t c = (unsigned char)*s; h1 = (h1 ^ c) * 2654435761u; h2 = (h2 ^ c) * 1597334677u; }
    h1 = ((h1 ^ (h1 >> 16)) * 2246822507u) ^ ((h2 ^ (h2 >> 13)) * 3266489909u);
    h2 = ((h2 ^ (h2 >> 16)) * 2246822507u) ^ ((h1 ^ (h1 >> 13)) * 3266489909u);
    uint64_t v = ((uint64_t)(h2 & 2097151u) << 32) + h1; char t[24]; int n = 0;
    if (!v) t[n++] = '0';
    while (v) { int d = (int)(v % 36); t[n++] = d < 10 ? '0' + d : 'a' + d - 10; v /= 36; }
    for (int i = 0; i < n; i++) out[i] = t[n - 1 - i];
    out[n] = 0;
}
static int todayNum(void) { pspTime t; sceRtcGetCurrentClockLocalTime(&t); return t.year * 10000 + t.month * 100 + t.day; }

/* ------------------------------------------------------------ misi */
static const int MGOAL[5] = {50, 25, 120, 4, 5000000};
static int mval(int i) {
    switch (i) { case 0: return S.mbat; case 1: return S.mrun; case 2: return S.mspd;
        case 3: { int n = 0; for (int k = 0; k < NCARS; k++) if (S.owned[k] && !CARS[k].lim) n++; return n; }
        default: return S.mearn; }
}
static int msDone(void) { for (int i = 0; i < 5; i++) if (mval(i) < MGOAL[i]) return 0; return 1; }
static char toastS[96]; static int toastT = 0;
static void toast(const char *s) { snprintf(toastS, sizeof(toastS), "%s", s); toastT = 100; }
static void chkMs(void) {
    int ch = 0;
    for (int i = 0; i < 5; i++) if (mval(i) >= MGOAL[i] && !S.done[i]) { S.done[i] = 1; ch = 1;
        char b[96]; snprintf(b, sizeof(b), "%s %d/5", TR("MISI SELESAI", "MISSION COMPLETE"), i + 1); toast(b); }
    if (ch) saveGame();
}

/* ------------------------------------------------------------ cuaca */
typedef struct { u32 bg; float fn, ff; u32 ground, road; float grip; u32 ov; int night, rain, snow; const char *id, *en; } Wx;
static const Wx WX[4] = {
    {0xFFEBCE87u, 60, 230, 0, 0, 1.0f, 0, 0, 0, 0, "PAGI", "MORNING"},
    {0xFF4A2B16u, 30, 190, 0, 0, 1.0f, RGBA(5, 8, 24, 130), 1, 0, 0, "MALAM", "NIGHT"},
    {0xFFE3D8CFu, 25, 150, 0, 0, .72f, 0, 0, 0, 1, "SALJU", "SNOW"},
    {0xFF70665Cu, 20, 140, 0, 0, .85f, RGBA(60, 70, 80, 50), 0, 1, 0, "HUJAN", "RAIN"}};
static u32 wxGround(int i) { return i == 2 ? HEX(0xf2f6fa) : (i == 3 ? HEX(0x3f7a35) : HEX(0x5fae4a)); }
static u32 wxRoad(int i) { return i == 2 ? HEX(0xd9e0e6) : (i == 3 ? HEX(0x2a2c30) : HEX(0x38393d)); }
static int WI = 0;

/* ------------------------------------------------------------ jalan */
static Vtx *roadV; static int roadN;
static void addQ(int *k, u32 c, float x0, float z0, float x1, float z1, float x2, float z2, float x3, float z3) {
    Vtx *v = roadV; float px[4] = {x0, x1, x2, x3}, pz[4] = {z0, z1, z2, z3};
    for (int i = 0; i < 6; i++) { int q = QI[i]; v[*k].c = c; v[*k].x = px[q]; v[*k].y = 0; v[*k].z = pz[q]; (*k)++; }
}
static void ringQ(int *k, float r1, float r2, int n, u32 ca, u32 cb, int skipOdd) {
    for (int i = 0; i < n; i++) {
        if (skipOdd && (i & 1)) continue;
        float a0 = i * 2 * PI / n, a1 = (i + 1) * 2 * PI / n; u32 c = (i & 1) ? cb : ca;
        addQ(k, c, sinf(a0) * r1, cosf(a0) * r1, sinf(a0) * r2, cosf(a0) * r2, sinf(a1) * r2, cosf(a1) * r2, sinf(a1) * r1, cosf(a1) * r1);
    }
}
static void buildRoads(void) {
    if (!roadV) roadV = (Vtx *)memalign(16, 6000 * sizeof(Vtx));
    int k = 0; u32 rd = wxRoad(WI), rw = HEX(0xe53935), ww = HEX(0xf5f5f5);
    ringQ(&k, 41.5f, 43.4f, 48, HEX(0x8d7b5a), HEX(0x8d7b5a), 0);
    ringQ(&k, 56.6f, 58.5f, 48, HEX(0x8d7b5a), HEX(0x8d7b5a), 0);
    ringQ(&k, 44, 56, 64, rd, rd, 0); ringQ(&k, 124, 136, 64, rd, rd, 0);
    ringQ(&k, 43.4f, 44, 96, rw, ww, 0); ringQ(&k, 56, 56.6f, 96, rw, ww, 0);
    ringQ(&k, 123.4f, 124, 96, rw, ww, 0); ringQ(&k, 136, 136.6f, 96, rw, ww, 0);
    addQ(&k, rd, -6, -130, 6, -130, 6, 130, -6, 130);
    addQ(&k, rd, -130, -6, -130, 6, 130, 6, 130, -6);
    ringQ(&k, 49.85f, 50.15f, 64, ww, ww, 1); ringQ(&k, 129.85f, 130.15f, 128, ww, ww, 1);
    roadN = k; sceKernelDcacheWritebackRange(roadV, 6000 * sizeof(Vtx));
}

/* ------------------------------------------------------------ dunia */
typedef struct { float x, z, r; } Obst;
static Obst obst[160]; static int nObst;
typedef struct { float x, z, face, w, hh; int roof, wall; } House;
static House houses[64]; static int nHouse;
typedef struct { float x, z, face, w, hh; int c; } Bld;
static Bld blds[16]; static int nBld;
typedef struct { float x, z, s; int c; } Tree;
static Tree trees[80]; static int nTree;
typedef struct { float x, z, face; } Lamp;
static Lamp lamps[80]; static int nLamp;
static const u32 WALLC[4] = {0xe8d5b0, 0xd9a7a0, 0xb7cbd8, 0xc9d9a8}, ROOFC[3] = {0x8d3b2f, 0x4e5d6c, 0x6d4c2f},
                 BLDC[3] = {0x9aa7b5, 0xb59a8a, 0x7f93a8}, TREEC[4] = {0x2e7d32, 0x388e3c, 0x43a047, 0x558b2f};
static void ringObj(int n, float r, float face, int kind) {
    for (int i = 0; i < n; i++) {
        float a = (i + .5f) / n * 2 * PI, x = sinf(a) * r, z = cosf(a) * r;
        if (fabsf(x) < 11 || fabsf(z) < 11) continue;
        float fa = atan2f(face * x, face * z);
        if (kind == 0 && nHouse < 64) { House *h = &houses[nHouse++]; h->x = x; h->z = z; h->face = fa; h->w = 7 + rnd() * 3; h->hh = 4 + rnd() * 1.5f;
            h->roof = rand() % 3; h->wall = rand() % 4; obst[nObst].x = x; obst[nObst].z = z; obst[nObst++].r = h->w * .72f; }
        else if (kind == 1 && nBld < 16) { Bld *b = &blds[nBld++]; b->x = x; b->z = z; b->face = fa; b->w = 9 + rnd() * 4; b->hh = 26 + rnd() * 26;
            b->c = rand() % 3; obst[nObst].x = x; obst[nObst].z = z; obst[nObst++].r = b->w * .72f; }
        else if (kind == 2 && nLamp < 80) { lamps[nLamp].x = x; lamps[nLamp].z = z; lamps[nLamp++].face = fa; }
    }
}
static void initWorld(void) {
    srand(7);
    ringObj(9, 34, 1, 0); ringObj(18, 67, -1, 0); ringObj(26, 112, 1, 0); ringObj(11, 89, 1, 1);
    ringObj(14, 59, -1, 2); ringObj(10, 40.5f, 1, 2); ringObj(22, 138, -1, 2); ringObj(18, 122, 1, 2);
    for (int i = 0; i < 80; i++) { float a = rnd() * 2 * PI, r = 147 + rnd() * 45;
        trees[nTree].x = sinf(a) * r; trees[nTree].z = cosf(a) * r; trees[nTree].s = .8f + rnd() * .7f; trees[nTree++].c = rand() % 4; }
    buildRoads();
}

/* koin */
#define NCOIN 30
typedef struct { float x, z; int k; } Coin;
static Coin coins[NCOIN];
static void placeCoin(Coin *c) {
    float q = rnd(); c->k = q < .6f ? 0 : (q < .9f ? 1 : 2);
    float x, z; int n = 0;
    do { float a = rnd() * 2 * PI, r = rnd() < .3f ? 44 + rnd() * 12 : 10 + rnd() * 115; x = sinf(a) * r; z = cosf(a) * r;
         int bad = 0; for (int i = 0; i < nObst; i++) if (hypotf(x - obst[i].x, z - obst[i].z) < obst[i].r + 2.5f) { bad = 1; break; }
         if (!bad) break; } while (n++ < 30);
    c->x = x; c->z = z;
}
static const int COINV[3] = {10000, 25000, 50000};
static const u32 COINC[3] = {0xcd7f32, 0xffc107, 0x4dd0e1};

/* lalu lintas */
typedef struct { int kind, cidx, ci; float r, dir, a, a0, v, spin, x, z, h; } Veh;
static Veh traffic[7];
static void initTraffic(void) {
    static const float P[7][6] = {{47.5f,-1,0,1,1,9},{52.5f,1,1,2.5f,0,12},{52.5f,1,0,4.6f,2,12},{47.5f,-1,1,3.6f,0,9},
                                  {127.5f,-1,0,.7f,0,11},{132.5f,1,1,2.8f,0,14},{132.5f,1,0,5,3,14}};
    static const int CI[7] = {1, 0, 2, 0, 0, 0, 3};
    for (int i = 0; i < 7; i++) { Veh *v = &traffic[i]; v->r = P[i][0]; v->dir = P[i][1]; v->kind = (int)P[i][2]; v->a0 = v->a = P[i][3];
        v->v = P[i][5]; v->ci = CI[i]; v->cidx = i % 3; }
}
static void trafficUpdate(void) {
    for (int i = 0; i < 7; i++) { Veh *v = &traffic[i];
        v->a += v->dir * v->v / v->r * DT; v->x = sinf(v->a) * v->r; v->z = cosf(v->a) * v->r;
        v->h = atan2f(v->dir * cosf(v->a), -v->dir * sinf(v->a)); v->spin += v->v * DT / .4f; }
}

/* ------------------------------------------------------------ state */
enum { ST_INTRO, ST_LOBBY, ST_GARAGE, ST_PLAY, ST_OVER, ST_MIS, ST_SPIN, ST_CODE };
static int state = ST_INTRO, running = 1, frame = 0;
static int ci = 0, pi = 0, selL = 0;
static float ang = 0, showSc = 0;
typedef struct { float x, z, h, v, dir, steer, shake; } Player;
static Player P;
static int score, earnedRun, batPend, batOn, batSpawned, hitCD;
static float tleft, bx, bz, rollZ, rollX, wspin, camx, camz, camy, camfov;
static int bestRun;
static SceCtrlData pad; static unsigned prevB; static unsigned pressed;
static float axisX;

static float effSpd(int c) { return CARS[c].spd * (1 + .045f * (S.lv[c] - 1)); }
static float effAcc(int c) { return CARS[c].acc * (1 + .05f * (S.lv[c] - 1)); }
static float effHnd(int c) { return CARS[c].hnd * (1 + .03f * (S.lv[c] - 1)); }
static int upCost(int c) { float p = CARS[c].price ? (float)CARS[c].price : 150000.0f; return (int)(((p * .08f + 15000) * S.lv[c]) / 1000 + .5f) * 1000; }

static void batReset(void) { batSpawned = 0; batPend = 0; batOn = 0; }
static void spawnBat(void) {
    float x, z; int n = 0;
    do { float a = rnd() * 2 * PI, r = rnd() < .4f ? 44 + rnd() * 12 : 12 + rnd() * 110; x = sinf(a) * r; z = cosf(a) * r;
         int bad = 0; for (int i = 0; i < nObst; i++) if (hypotf(x - obst[i].x, z - obst[i].z) < obst[i].r + 2.5f) { bad = 1; break; }
         if (hypotf(x - P.x, z - P.z) < 30) bad = 1; if (!bad) break; } while (n++ < 40);
    bx = x; bz = z; batOn = 1; batSpawned = 1;
    toast(TR("BATMAN COIN MUNCUL! AMBIL CEPAT", "BATMAN COIN APPEARED! GRAB IT"));
}
static void startGame(void) {
    state = ST_PLAY; score = 0; tleft = 60; earnedRun = 0; memset(&P, 0, sizeof(P));
    P.x = 0; P.z = 50; P.h = P.dir = PI / 2; camx = -9; camz = 50; camy = 4.8f; camfov = 55; rollZ = rollX = wspin = 0; hitCD = 0;
    for (int i = 0; i < NCOIN; i++) placeCoin(&coins[i]);
    for (int i = 0; i < 7; i++) traffic[i].a = traffic[i].a0;
    batReset();
}
static void finishRun(void) {
    S.money += earnedRun; S.mearn += earnedRun; S.mbat += batPend; batPend = 0; batOn = 0;
    if (score > S.best) S.best = score; chkMs(); saveGame();
}

static void updatePlay(void) {
    const CarDef *d = &CARS[ci]; float sp = effSpd(ci), ac = effAcc(ci), hn = effHnd(ci);
    int up = (pad.Buttons & (PSP_CTRL_CROSS | PSP_CTRL_RTRIGGER)) != 0;
    int dn = (pad.Buttons & (PSP_CTRL_SQUARE | PSP_CTRL_LTRIGGER)) != 0;
    int hb = (pad.Buttons & PSP_CTRL_CIRCLE) != 0;
    float grip = WX[WI].grip;
    if (up) P.v += ac * DT * (P.v < 0 ? 2 : 1);
    else if (dn) P.v -= ac * (P.v > 0 ? 2 : .8f) * DT;
    else P.v *= 1 - minf(1, .7f * DT);
    P.v = clampf(P.v, -sp * .4f, sp);
    P.steer += (axisX - P.steer) * minf(1, DT * 8);
    if (hb) P.v *= 1 - minf(1, .5f * DT);
    P.h += P.steer * hn * 1.3f * DT * clampf(P.v / 7, -1, 1) * (hb ? 1.5f : 1) / (1 + fabsf(P.v) / 45);
    P.dir += (P.h - P.dir) * minf(1, DT * grip * (hb ? 1.6f : (dn && P.v > 8 ? 3 : 7)));
    P.x += sinf(P.dir) * P.v * DT; P.z += cosf(P.dir) * P.v * DT;
    float dist = hypotf(P.x, P.z);
    if ((dist < 44 || dist > 56) && P.v > sp * .7f) P.v *= 1 - DT * .8f;
    if (dist > 140) { P.x *= 140 / dist; P.z *= 140 / dist; P.v *= .6f; }
    hitCD--;
    for (int i = 0; i < nObst; i++) {
        float dx = P.x - obst[i].x, dz = P.z - obst[i].z, dd = hypotf(dx, dz), mn = obst[i].r + 1.6f;
        if (dd < mn) { float k = (mn - dd) / (dd > .001f ? dd : 1); P.x += dx * k; P.z += dz * k;
            if (hitCD <= 0 && fabsf(P.v) > 6) { hitCD = 12; P.shake = .5f; }
            P.v *= .55f; }
    }
    for (int i = 0; i < 7; i++) {
        float dx = P.x - traffic[i].x, dz = P.z - traffic[i].z, dd = hypotf(dx, dz), mn = (traffic[i].kind ? 1.1f : 1.7f) + 1.5f;
        if (dd < mn) { float k = (mn - dd) / (dd > .001f ? dd : 1); P.x += dx * k; P.z += dz * k;
            if (hitCD <= 0 && fabsf(P.v) > 6) { hitCD = 12; P.shake = .5f; } P.v *= .7f; }
    }
    for (int i = 0; i < NCOIN; i++) if (hypotf(P.x - coins[i].x, P.z - coins[i].z) < 2.6f) {
        score++; S.mcoins++; if (score > S.mrun) S.mrun = score; earnedRun += COINV[coins[i].k];
        if (score >= 10 && !batSpawned) spawnBat();
        placeCoin(&coins[i]); chkMs();
    }
    if (batOn && hypotf(P.x - bx, P.z - bz) < 3) { batOn = 0; batPend++; toast(TR("+1 BATMAN COIN (DIHITUNG SETELAH WAKTU HABIS)", "+1 BATMAN COIN (COUNTED AT END)")); }
    int kmh = (int)(fabsf(P.v) * 3.6f); if (kmh > S.mspd) S.mspd = kmh; chkMs();
    tleft -= DT;
    if (tleft <= 0) { tleft = 0; finishRun(); state = ST_OVER; return; }
    float slip = P.h - P.dir, av = fabsf(P.v), spf = minf(1, av / 20), k6 = minf(1, DT * 6);
    rollZ += ((P.steer * spf * .07f + slip * .15f) - rollZ) * k6;
    rollX += (((dn && P.v > 0 ? .05f : 0) - (up ? .03f * minf(1, av / 10) : 0)) - rollX) * k6;
    wspin += P.v * DT / d->wr;
    float fx = sinf(P.h), fz = cosf(P.h), rx = cosf(P.h), rz = -sinf(P.h);
    float tx = P.x - fx * 9 - rx * P.steer * 1.2f, tz = P.z - fz * 9 - rz * P.steer * 1.2f, kk = minf(1, DT * 5);
    camx += (tx - camx) * kk; camz += (tz - camz) * kk; camy += (4.8f - camy) * minf(1, DT * 3);
    if (P.shake > 0) { camx += (rnd() - .5f) * P.shake; camy += (rnd() - .5f) * P.shake * .6f; P.shake = P.shake - DT * 1.5f; if (P.shake < 0) P.shake = 0; }
    camfov += ((55 + av / sp * 12) - camfov) * DT * 3;
}

/* ------------------------------------------------------------ render 3D */
static float vex, vez, vfx, vfz;
static int vis(float x, float z, float rad, float maxd) {
    float dx = x - vex, dz = z - vez;
    if (dx * dx + dz * dz > maxd * maxd) return 0;
    if (dx * vfx + dz * vfz < -rad) return 0;
    return 1;
}
static u32 carPaint(int c, int pidx) {
    if (CARS[c].fl & F_RAINBOW) return S.owned[c] ? hsvCol(floorf(frame / 30.0f * .15f * 24) / 24.0f) : HEX(0x0a0a0a);
    return HEX(CARS[c].col[pidx]);
}
static float cloudX[8];

static void scene3D(float ex, float ey, float ez, float cx, float cy, float cz, float fov, int play) {
    sceGuEnable(GU_FOG); sceGuFog(WX[WI].fn, WX[WI].ff, WX[WI].bg);
    sceGumMatrixMode(GU_PROJECTION); sceGumLoadIdentity(); sceGumPerspective(fov, (float)SCR_W / SCR_H, 1.0f, 300.0f);
    sceGumMatrixMode(GU_VIEW); sceGumLoadIdentity();
    ScePspFVector3 eye = {ex, ey, ez}, ctr = {cx, cy, cz}, upv = {0, 1, 0};
    sceGumLookAt(&eye, &ctr, &upv);
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();
    float l = hypotf(cx - ex, cz - ez); vex = ex; vez = ez; vfx = (cx - ex) / (l > .001f ? l : 1); vfz = (cz - ez) / (l > .001f ? l : 1);
    sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_TEXTURE_2D);
    { Vtx *g = (Vtx *)sceGuGetMemory(4 * sizeof(Vtx)); u32 gc = wxGround(WI); float s = 600;
      g[0].c = g[1].c = g[2].c = g[3].c = gc; g[0].y = g[1].y = g[2].y = g[3].y = 0;
      g[0].x = -s; g[0].z = -s; g[1].x = s; g[1].z = -s; g[2].x = -s; g[2].z = s; g[3].x = s; g[3].z = s;
      sceGumDrawArray(GU_TRIANGLE_STRIP, VF3, 4, 0, g); }
    sceGumDrawArray(GU_TRIANGLES, VF3, roadN, 0, roadV);
    /* bayangan */
    sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    if (play) {
        const CarDef *d = &CARS[ci]; ScePspFVector3 t = {P.x, 0.05f, P.z};
        sceGumPushMatrix(); sceGumTranslate(&t); sceGumRotateY(P.h);
        dm(M_CUBE, RGBA(0, 0, 0, 80), 0, 0, 0, d->w * .95f, .02f, d->l * .9f, 0, 0, 0); sceGumPopMatrix();
        for (int i = 0; i < 7; i++) if (vis(traffic[i].x, traffic[i].z, 5, 90)) {
            ScePspFVector3 q = {traffic[i].x, 0.05f, traffic[i].z};
            sceGumPushMatrix(); sceGumTranslate(&q); sceGumRotateY(traffic[i].h);
            dm(M_CUBE, RGBA(0, 0, 0, 80), 0, 0, 0, traffic[i].kind ? .7f : 1.8f, .02f, traffic[i].kind ? 1.8f : 4, 0, 0, 0); sceGumPopMatrix(); }
    }
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
    int snow = (WI == 2);
    /* rumah */
    for (int i = 0; i < nHouse; i++) { House *h = &houses[i]; if (!vis(h->x, h->z, 8, 130)) continue;
        ScePspFVector3 t = {h->x, 0, h->z}; sceGumPushMatrix(); sceGumTranslate(&t); sceGumRotateY(h->face);
        u32 rc = HEX(ROOFC[h->roof]); if (snow) rc = mixw(rc, .8f);
        BX(HEX(WALLC[h->wall]), 0, h->hh / 2, 0, h->w, h->hh, h->w);
        dm(M_PYR4, rc, 0, h->hh, 0, h->w + 1.2f, 3, h->w + 1.2f, 0, 0, 0);
        BX(HEX(0x5a3a22), 0, 1.1f, h->w / 2 + .04f, 1.2f, 2.2f, .12f);
        BX(HEX(0x7a4a3a), h->w * .25f, h->hh + 2, -h->w * .15f, .8f, 2.2f, .8f);
        BX(HEX(0x2f7d32), -h->w * .32f, .5f, h->w / 2 + .9f, 1.2f, 1.0f, 1.2f); BX(HEX(0x2f7d32), h->w * .32f, .5f, h->w / 2 + .9f, 1.2f, 1.0f, 1.2f);
        sceGumPopMatrix(); }
    for (int i = 0; i < nBld; i++) { Bld *b = &blds[i]; if (!vis(b->x, b->z, 10, 150)) continue;
        ScePspFVector3 t = {b->x, 0, b->z}; sceGumPushMatrix(); sceGumTranslate(&t); sceGumRotateY(b->face);
        BX(HEX(BLDC[b->c]), 0, b->hh / 2, 0, b->w, b->hh, b->w);
        u32 wc = WX[WI].night ? HEX(0xffd27a) : HEX(0x27384d);
        for (int k = 0; k < 7; k++) BX(wc, 0, 3 + k * (b->hh - 6) / 6.0f, 0, b->w + .06f, 1.3f, b->w + .06f);
        BX(HEX(0x555b62), 0, b->hh + .25f, 0, b->w + .4f, .5f, b->w + .4f);
        BX(HEX(0x555b62), 0, b->hh + 3, 0, .18f, 5, .18f); BX(HEX(0xff2222), 0, b->hh + 5.4f, 0, .5f, .5f, .5f);
        sceGumPopMatrix(); }
    for (int i = 0; i < nTree; i++) { Tree *t = &trees[i]; if (!vis(t->x, t->z, 4, 190)) continue;
        u32 cc = HEX(TREEC[t->c]); if (snow) cc = mixw(cc, .7f); float s = t->s;
        dm(M_CYL, HEX(0x6d4c2f), t->x, 1.3f * s, t->z, .7f * s, 2.6f * s, .7f * s, 0, 0, 0);
        dm(M_CONE8, cc, t->x, 1.4f * s, t->z, 3.6f * s, 4.2f * s, 3.6f * s, 0, 0, 0);
        dm(M_CONE8, cc, t->x, 3.6f * s, t->z, 2.6f * s, 3.0f * s, 2.6f * s, 0, 0, 0); }
    for (int i = 0; i < nLamp; i++) { Lamp *lp = &lamps[i]; if (!vis(lp->x, lp->z, 3, 85)) continue;
        ScePspFVector3 t = {lp->x, 0, lp->z}; sceGumPushMatrix(); sceGumTranslate(&t); sceGumRotateY(lp->face);
        dm(M_CYL, HEX(0x3a3f45), 0, 3.6f, 0, .25f, 7.2f, .25f, 0, 0, 0);
        BX(HEX(0x3a3f45), 0, 7.15f, .9f, .12f, .12f, 1.9f);
        BX(HEX(0x3a3f45), 0, 7.05f, 1.8f, .5f, .16f, .9f);
        BX(WX[WI].night ? HEX(0xffe6a8) : HEX(0x888888), 0, 6.92f, 1.8f, .4f, .06f, .7f);
        sceGumPopMatrix(); }
    for (int i = 0; i < 8; i++) BX(WI == 3 ? HEX(0x555d66) : (WI == 1 ? HEX(0x2a3350) : HEX(0xffffff)), cloudX[i], 85 + (i % 3) * 8, -200 + i * 50.0f, 34, 5, 14);
    if (play) {
        for (int i = 0; i < NCOIN; i++) if (vis(coins[i].x, coins[i].z, 3, 110)) {
            float s = i % 3 == 0 ? 1.0f : (coins[i].k == 1 ? 1.15f : (coins[i].k == 2 ? 1.4f : 1.0f));
            dm(M_CYL, HEX(COINC[coins[i].k]), coins[i].x, 1.2f + sinf(frame * .1f + i) * .15f, coins[i].z, 1.6f * s, .25f, 1.6f * s, frame * .1f, 0, PI / 2); }
        if (batOn) { float y = 1.4f + sinf(frame * .1f) * .18f; ScePspFVector3 t = {bx, y, bz};
            sceGumPushMatrix(); sceGumTranslate(&t); sceGumRotateY(frame * .08f);
            dm(M_CYL, HEX(0xf5c518), 0, 0, 0, 2.5f, .26f, 2.5f, 0, 0, PI / 2);
            dm(M_CYL, HEX(0x0b0b0d), .02f, 0, 0, 2.0f, .3f, 2.0f, 0, 0, PI / 2);
            BX(HEX(0xf5c518), 0, 0, 0, .34f, .6f, 1.3f);
            sceGumPopMatrix(); }
    }
    for (int i = 0; i < 7; i++) { Veh *v = &traffic[i]; if (!vis(v->x, v->z, 4, 110)) continue;
        if (v->kind) drawBike(HEX(0xd32f2f + 0x101010 * i), v->x, v->z, v->h, v->spin, 1);
        else { const CarDef *d = &CARS[v->ci]; drawCar(d, HEX(d->col[v->cidx]), v->x, .08f, v->z, v->h, 0, v->spin, 0, 0, 0, 1); } }
    if (play) {
        const CarDef *d = &CARS[ci];
        int brake = (pad.Buttons & (PSP_CTRL_SQUARE | PSP_CTRL_LTRIGGER)) != 0;
        drawCar(d, carPaint(ci, pi), P.x, .1f, P.z, P.h, P.steer, wspin, rollZ, rollX, brake, 1);
    } else {
        const CarDef *d = &CARS[ci]; float sc = showSc;
        dm(M_CYL, HEX(0x2b3552), -2.2f, .1f, 0, 9, .3f, 9, 0, 0, 0);
        drawCar(d, carPaint(ci, pi), -2.2f, .25f + sinf(frame * .07f) * .015f, 0, ang, sinf(frame * .03f) * .25f, 0, 0, 0, 0, sc);
    }
    sceGuDisable(GU_FOG);
}

/* ------------------------------------------------------------ partikel cuaca */
static float wpx[90], wpy[90];
static void initParticles(void) { for (int i = 0; i < 90; i++) { wpx[i] = rnd() * SCR_W; wpy[i] = rnd() * SCR_H; } }
static float starX[50], starY[50];
static void drawWeatherFx(void) {
    if (WX[WI].rain) for (int i = 0; i < 90; i++) { wpy[i] += 16; wpx[i] -= 3; if (wpy[i] > SCR_H) { wpy[i] -= SCR_H + 10; wpx[i] = rnd() * SCR_W; } if (wpx[i] < 0) wpx[i] += SCR_W;
        line2(wpx[i], wpy[i], wpx[i] - 2, wpy[i] + 8, RGBA(200, 215, 255, 150)); }
    if (WX[WI].snow) for (int i = 0; i < 90; i++) { wpy[i] += 2.2f; wpx[i] += sinf(frame * .1f + i); if (wpy[i] > SCR_H) { wpy[i] -= SCR_H + 4; wpx[i] = rnd() * SCR_W; }
        rect(wpx[i], wpy[i], 2, 2, RGBA(255, 255, 255, 230)); }
}

/* ------------------------------------------------------------ UI */
static void ui2dBegin(void) {
    sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_FOG); sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}
static void moneyBar(void) {
    char b[40]; fmtM(S.money + (state == ST_PLAY ? earnedRun : 0), b);
    float w = textw(b, 2) + 12; panel(SCR_W - w - 6, 5, w, 20, RGBA(15, 20, 35, 200)); texts(SCR_W - w, 9, b, 2, RGB(185, 246, 202));
}
static void hudDraw(void) {
    char b[64];
    panel(5, 5, 78, 20, RGBA(15, 20, 35, 190)); snprintf(b, sizeof(b), "COIN %d", score); texts(10, 9, b, 2, RGB(255, 179, 71));
    snprintf(b, sizeof(b), "%d", (int)ceilf(tleft)); panel(88, 5, 62, 20, tleft <= 10 && (frame & 8) ? RGBA(200, 30, 30, 220) : RGBA(15, 20, 35, 190));
    texts(94, 9, b, 2, RGB(255, 179, 71));
    snprintf(b, sizeof(b), "BAT %d", S.mbat + batPend); panel(155, 5, 78, 20, RGBA(15, 20, 35, 190)); texts(160, 9, b, 2, RGB(245, 197, 24));
    snprintf(b, sizeof(b), "%d", (int)(fabsf(P.v) * 3.6f)); panel(5, SCR_H - 52, 92, 46, RGBA(15, 20, 35, 190));
    texts(12, SCR_H - 46, b, 4, RGB(255, 255, 255)); texts(12, SCR_H - 20, TR("KM/J", "KM/H"), 1.5f, RGB(255, 179, 71));
    moneyBar();
    /* panah koin terdekat */
    float nx = 0, nz = 0, nd = 1e9f;
    for (int i = 0; i < NCOIN; i++) { float q = hypotf(coins[i].x - P.x, coins[i].z - P.z); if (q < nd) { nd = q; nx = coins[i].x; nz = coins[i].z; } }
    if (batOn) { nx = bx; nz = bz; nd = hypotf(bx - P.x, bz - P.z); }
    float dx = nx - P.x, dz = nz - P.z, A = dx * cosf(P.h) - dz * sinf(P.h), B = dx * sinf(P.h) + dz * cosf(P.h), th = atan2f(A, B);
    float ox = 40, oy = 56, r = 16, sx = -sinf(th), sy = -cosf(th);
    panel(ox - 24, oy - 24, 48, 56, RGBA(15, 20, 35, 150));
    tri2(ox + sx * r, oy + sy * r, ox - sy * r * .6f - sx * r * .6f, oy + sx * r * .6f - sy * r * .6f, ox + sy * r * .6f - sx * r * .6f, oy - sx * r * .6f - sy * r * .6f, RGB(255, 193, 7));
    snprintf(b, sizeof(b), "%dM", (int)nd); textc(ox, oy + 22, b, 1.5f, RGB(255, 255, 255));
}
static void drawToast(void) {
    if (toastT > 0) { toastT--; float w = textw(toastS, 1.5f) + 16; if (w > 470) w = 470;
        panel(SCR_W / 2 - w / 2, 34, w, 20, RGBA(15, 20, 35, 230)); rect(SCR_W / 2 - w / 2, 34, w, 1, RGB(255, 213, 79));
        text(SCR_W / 2 - w / 2 + 8, 40, toastS, 1.5f, RGB(255, 255, 255)); }
}

static void drawIntro(void) {
    ui2dBegin();
    float t = frame / 30.0f;
    for (int i = 0; i < 40; i++) rect((i * 97) % SCR_W, (i * 53) % SCR_H, 2, 2, (i & 1) ? RGB(255, 59, 74) : RGB(59, 139, 255));
    float a = clampf(t / .6f, 0, 1);
    textc(SCR_W / 2, 60 - (1 - a) * 40, "CAR", 8, RGB(255, 45, 61));
    textc(SCR_W / 2, 118 - (1 - clampf((t - .5f) / .6f, 0, 1)) * 40, "DRIVING", 8, RGB(45, 139, 255));
    if (t > 1.6f) textc(SCR_W / 2, 188, "BY CAVERO AND CHRISTIAN", 2, RGB(255, 213, 79));
    rect(SCR_W / 2 - 100, 176, 200 * clampf((t - 1) / 1, 0, 1), 3, RGB(255, 45, 61));
    float cxp = -140 + clampf((t - 1.2f) / 2.0f, 0, 1) * 760;
    rect(cxp, 226, 90, 14, RGB(229, 57, 53)); rect(cxp + 22, 214, 40, 13, RGB(229, 57, 53));
    rect(cxp + 26, 217, 32, 8, RGB(159, 208, 255));
    rect(cxp + 8, 238, 16, 16, RGB(17, 17, 17)); rect(cxp + 62, 238, 16, 16, RGB(17, 17, 17));
    if (frame > 20 && (frame & 16)) textc(SCR_W / 2, 262, TR("TEKAN X", "PRESS X"), 1, RGB(159, 179, 255));
}

static int canSpin(void) { return S.lastSpin != todayNum(); }
static const char *MNAME_ID[5] = {"PEMBURU BATMAN COIN", "RAKUS KOIN", "NGEBUT MAKSIMAL", "GARASI IMPIAN", "SULTAN JALANAN"};
static const char *MNAME_EN[5] = {"BAT COIN HUNTER", "COIN RUSH", "TOP SPEED", "DREAM GARAGE", "ROAD TYCOON"};
static const char *MDESC_ID[5] = {"KUMPULKAN 50 BATMAN COIN (MUNCUL SETELAH 10 KOIN)", "KUMPULKAN 25 KOIN DALAM 1 RONDE",
                                  "CAPAI KECEPATAN 120 KM/J", "MILIKI 4 MOBIL BIASA", "HASILKAN TOTAL RP 5.000.000"};
static const char *MDESC_EN[5] = {"COLLECT 50 BATMAN COINS (APPEARS AFTER 10 COINS)", "COLLECT 25 COINS IN ONE ROUND",
                                  "REACH 120 KM/H", "OWN 4 REGULAR CARS", "EARN RP 5,000,000 IN TOTAL"};

static void drawLobby(void) {
    ui2dBegin();
    texts(14, 22, "CAR", 6, RGB(255, 59, 74));
    texts(14, 70, "DRIVING", 5, RGB(59, 139, 255));
    texts(14, 8, "GAME BY CAVERO AND CHRISTIAN", 1.2f, RGB(255, 255, 255));
    panel(262, 34, 210, 196, RGBA(15, 20, 35, 205));
    char b[64]; const char *it[6];
    snprintf(b, sizeof(b), "%s %s", TR("CUACA:", "WEATHER:"), LANG ? WX[WI].en : WX[WI].id);
    static char wb[64]; snprintf(wb, sizeof(wb), "%s", b);
    it[0] = TR("MAIN", "PLAY"); it[1] = TR("MISI", "MISSIONS"); it[2] = "SPIN"; it[3] = TR("KODE", "CODE");
    it[4] = wb; it[5] = LANG ? "LANGUAGE: ENGLISH" : "BAHASA: INDONESIA";
    for (int i = 0; i < 6; i++) {
        float y = 44 + i * 28;
        if (i == selL) rect(266, y - 4, 202, 24, RGBA(255, 122, 0, 210));
        texts(274, y, it[i], 1.6f, RGB(255, 255, 255));
    }
    if (canSpin()) texts(440, 44 + 2 * 28, "!", 1.6f, RGB(255, 255, 80));
    if (msDone() && !S.owned[BATI]) texts(440, 44 + 1 * 28, "!", 1.6f, RGB(255, 255, 80));
    text(262, 236, TR("ATAS/BAWAH: PILIH   X: OK", "UP/DOWN: SELECT   X: OK"), 1, RGB(220, 220, 220));
    textc(SCR_W / 2, SCR_H - 12, "CHRISTIAN RAFAEL SIMANJUNTAK DAN CAVERO MITOKONA SIHOTANG", 1, RGB(200, 200, 200));
}

static void drawGarage(void) {
    ui2dBegin(); moneyBar();
    const CarDef *d = &CARS[ci]; int own = S.owned[ci]; char b[96], m1[40], m2[40];
    panel(246, 32, 228, 232, RGBA(15, 20, 35, 210));
    const char *nm = (d->secret && !own) ? "SECRET CAR" : d->n;
    texts(254, 40, nm, 2, RGB(255, 255, 255));
    if (own) { snprintf(b, sizeof(b), "%s  LV.%d", d->t, S.lv[ci]); texts(254, 60, b, 1.3f, RGB(255, 179, 71)); }
    else { snprintf(b, sizeof(b), "%s  [%s]", d->secret ? "SECRET CAR" : d->t, TR("TERKUNCI", "LOCKED")); texts(254, 60, b, 1.3f, RGB(255, 120, 120)); }
    const char *sl[3] = {TR("KECEPATAN", "SPEED"), TR("AKSELERASI", "ACCEL"), TR("KELINCAHAN", "HANDLING")};
    float sv[3] = {effSpd(ci) / 64.6f, effAcc(ci) / 27.5f, effHnd(ci) / 3.55f};
    if (d->secret && !own) sv[0] = sv[1] = sv[2] = 0;
    for (int i = 0; i < 3; i++) { text(254, 82 + i * 16, sl[i], 1.2f, RGB(220, 220, 220)); barr(350, 83 + i * 16, 114, 8, sv[i], RGB(255, 140, 20)); }
    if (own) for (int i = 0; i < 10; i++) rect(254 + i * 21, 134, 18, 6, i < S.lv[ci] ? ((i & 1) ? RGB(59, 139, 255) : RGB(255, 59, 74)) : RGB(43, 53, 82));
    if (!(d->secret && !own)) for (int i = 0; i < 3; i++) {
        u32 c = (d->fl & F_RAINBOW) ? hsvCol(frame / 30.0f * .3f + i * .3f) : HEX(d->col[i]);
        if (i == pi) rect(254 + i * 30 - 2, 148 - 2, 24, 24, RGB(255, 255, 255)); rect(254 + i * 30, 148, 20, 20, c); }
    if (own) { texts(254, 180, TR("X: MAIN", "X: PLAY"), 2, RGB(90, 208, 111)); }
    else if (d->lim) { texts(254, 180, d->secret ? TR("DAPAT DARI SPIN", "WIN IT IN SPIN") : TR("X: LIHAT MISI", "X: VIEW MISSIONS"), 1.4f, RGB(255, 213, 79)); }
    else { fmtM(d->price, m1); snprintf(b, sizeof(b), "X: %s %s", TR("BELI", "BUY"), m1);
        texts(254, 180, b, 1.4f, S.money >= d->price ? RGB(90, 208, 111) : RGB(150, 150, 150)); }
    if (own) { if (S.lv[ci] >= 10) texts(254, 204, TR("LEVEL MAKS", "MAX LEVEL"), 1.3f, RGB(200, 200, 200));
        else { fmtM(upCost(ci), m2); snprintf(b, sizeof(b), "%s: UPG LV%d %s", TR("KOTAK", "SQ"), S.lv[ci] + 1, m2);
            texts(254, 204, b, 1.1f, S.money >= upCost(ci) ? RGB(90, 208, 111) : RGB(150, 150, 150)); } }
    text(254, 226, TR("KIRI/KANAN: MOBIL  ATAS/BAWAH: WARNA", "L/R: CAR  UP/DOWN: COLOR"), 1, RGB(200, 200, 200));
    text(254, 240, TR("O: KEMBALI", "O: BACK"), 1, RGB(200, 200, 200));
    if (own) text(254, 252, TR("TOMBOL: X GAS  KOTAK REM  O DRIFT", "X GAS  SQUARE BRAKE  O DRIFT"), 1, RGB(200, 200, 200));
}

static void drawMis(void) {
    ui2dBegin(); rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 140));
    panel(30, 14, 420, 244, RGBA(15, 20, 35, 235)); textc(SCR_W / 2, 20, TR("MISI", "MISSIONS"), 3, RGB(255, 213, 79));
    char b[64];
    for (int i = 0; i < 5; i++) {
        float y = 52 + i * 36; int v = mval(i), g = MGOAL[i], ok = v >= g;
        texts(40, y, LANG ? MNAME_EN[i] : MNAME_ID[i], 1.4f, ok ? RGB(90, 208, 111) : RGB(255, 255, 255));
        snprintf(b, sizeof(b), "%d/%d", v > g ? g : v, g); text(440 - textw(b, 1.2f), y, b, 1.2f, RGB(255, 255, 255));
        barr(40, y + 13, 400, 6, (float)(v > g ? g : v) / g, RGB(255, 140, 20));
        text(40, y + 22, LANG ? MDESC_EN[i] : MDESC_ID[i], 1, RGB(180, 190, 210));
    }
    const char *c; u32 col;
    if (S.owned[BATI]) { c = TR("BATMAN LIMITED: SUDAH DIMILIKI", "BATMAN LIMITED: OWNED"); col = RGB(90, 208, 111); }
    else if (msDone()) { c = TR("X: KLAIM MOBIL BATMAN LIMITED!", "X: CLAIM BATMAN LIMITED CAR!"); col = RGB(255, 213, 79); }
    else { c = TR("SELESAIKAN 5 MISI = BATMAN LIMITED", "COMPLETE 5 MISSIONS = BATMAN LIMITED"); col = RGB(180, 190, 210); }
    textc(SCR_W / 2, 236, c, 1.4f, col);
    text(36, 249, TR("O: KEMBALI", "O: BACK"), 1, RGB(200, 200, 200));
}

static int spPhase = 0, spT = 0, spRes = 0; static float spTarget = 50, spMark = 50; static char spMsg[96] = "";
static void drawSpin(void) {
    ui2dBegin(); rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 140));
    panel(30, 24, 420, 224, RGBA(15, 20, 35, 235)); textc(SCR_W / 2, 32, "SPIN", 4, RGB(255, 213, 79));
    const float wd[4] = {.4f, .3f, .2f, .1f}; const u32 sc[4] = {RGB(46, 158, 72), RGB(217, 154, 0), RGB(30, 111, 217), RGB(60, 64, 80)};
    const char *lb[4] = {"100RB", "+5 BAT", TR("ULANG", "AGAIN"), "SECRET"}; float cx = 40;
    for (int i = 0; i < 4; i++) { rect(cx, 110, 400 * wd[i], 34, sc[i]); rect(cx, 110, 2, 34, RGB(255, 255, 255));
        text(cx + 400 * wd[i] / 2 - textw(lb[i], 1) / 2, 123, lb[i], 1, RGB(255, 255, 255)); cx += 400 * wd[i]; }
    float mx = 40 + 4 * spMark; tri2(mx, 106, mx - 8, 90, mx + 8, 90, RGB(255, 82, 82)); rect(mx - 1, 106, 2, 42, RGB(255, 82, 82));
    if (spMsg[0]) textc(SCR_W / 2, 164, spMsg, 1.8f, RGB(185, 246, 202));
    if (spPhase) textc(SCR_W / 2, 190, "...", 2, RGB(255, 255, 255));
    else textc(SCR_W / 2, 190, canSpin() ? TR("X: PUTAR GRATIS", "X: SPIN FREE") : TR("SUDAH DIPAKAI HARI INI", "ALREADY USED TODAY"), 1.8f, canSpin() ? RGB(90, 208, 111) : RGB(150, 150, 150));
    textc(SCR_W / 2, 218, TR("PELUANG: RP100RB 40%  +5 BAT COIN 30%  ULANG 20%  SECRET CAR 10%", "ODDS: RP100K 40%  +5 BAT COIN 30%  AGAIN 20%  SECRET CAR 10%"), 1, RGB(180, 190, 210));
    text(36, 234, TR("O: KEMBALI", "O: BACK"), 1, RGB(200, 200, 200));
}

#define NCS 37
static const char CSET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static int codeCh[12], codePos = 0, codeFails = 0, codeLock = 0; static char codeMsg[96] = "";
static void drawCode(void) {
    ui2dBegin(); rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 140));
    panel(24, 30, 432, 210, RGBA(15, 20, 35, 235)); textc(SCR_W / 2, 38, TR("KODE HADIAH", "REWARD CODE"), 3, RGB(255, 213, 79));
    textc(SCR_W / 2, 74, TR("MASUKKAN KODE UNTUK DAPAT UANG RP 2.000.000", "ENTER A CODE TO GET RP 2,000,000"), 1.2f, RGB(200, 210, 230));
    for (int i = 0; i < 12; i++) { float x = 36 + i * 34;
        rect(x, 100, 30, 38, i == codePos ? RGB(255, 213, 79) : RGB(90, 100, 130)); rect(x + 2, 102, 26, 34, RGB(16, 22, 43));
        char s[2] = {CSET[codeCh[i]], 0}; textc(x + 15, 110, s, 2.4f, RGB(255, 255, 255)); }
    if (codeMsg[0]) textc(SCR_W / 2, 154, codeMsg, 1.5f, RGB(255, 213, 79));
    textc(SCR_W / 2, 186, TR("ATAS/BAWAH: HURUF  KIRI/KANAN: POSISI", "UP/DOWN: LETTER  LEFT/RIGHT: SLOT"), 1.1f, RGB(200, 210, 230));
    textc(SCR_W / 2, 202, TR("X: KLAIM  KOTAK: HAPUS  O: KEMBALI", "X: REDEEM  SQUARE: CLEAR  O: BACK"), 1.1f, RGB(200, 210, 230));
    textc(SCR_W / 2, 220, TR("SETIAP KODE HANYA BISA DIPAKAI 1 KALI", "EACH CODE CAN BE USED ONCE"), 1, RGB(150, 160, 180));
}
static void submitCode(void) {
    char s[16]; int n = 0, i;
    for (i = 0; i < 12; i++) if (codeCh[i]) s[n++] = CSET[codeCh[i]];
    s[n] = 0;
    if (frame < codeLock) { snprintf(codeMsg, sizeof(codeMsg), "%s", TR("TERLALU BANYAK PERCOBAAN, TUNGGU", "TOO MANY ATTEMPTS, WAIT")); return; }
    if (!n) { snprintf(codeMsg, sizeof(codeMsg), "%s", TR("MASUKKAN KODE DULU", "ENTER A CODE FIRST")); return; }
    char h[24]; hashCode(s, h);
    for (i = 0; i < 8; i++) if (S.used[i][0] && strcmp(S.used[i], h) == 0) { snprintf(codeMsg, sizeof(codeMsg), "%s", TR("KODE SUDAH KAMU PAKAI", "YOU ALREADY USED THIS CODE")); return; }
    if (strcmp(h, "15s0lz7bl07") == 0) {
        for (i = 0; i < 8; i++) if (!S.used[i][0]) { strncpy(S.used[i], h, 15); break; }
        S.money += 2000000; saveGame(); codeFails = 0; memset(codeCh, 0, sizeof(codeCh)); codePos = 0;
        snprintf(codeMsg, sizeof(codeMsg), "%s +RP 2.000.000", TR("BERHASIL!", "SUCCESS!")); toast(codeMsg);
    } else { if (++codeFails >= 5) { codeFails = 0; codeLock = frame + 900; } snprintf(codeMsg, sizeof(codeMsg), "%s", TR("KODE SALAH", "INVALID CODE")); }
}

static void drawOver(void) {
    ui2dBegin(); rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 120));
    panel(100, 30, 280, 210, RGBA(15, 20, 35, 235)); char b[64], m[40];
    textc(SCR_W / 2, 40, TR("WAKTU HABIS!", "TIME'S UP!"), 3, RGB(255, 255, 255));
    textc(SCR_W / 2, 76, TR("KOIN TERKUMPUL", "COINS COLLECTED"), 1.3f, RGB(200, 210, 230));
    snprintf(b, sizeof(b), "%d", score); textc(SCR_W / 2, 92, b, 6, RGB(255, 179, 71));
    snprintf(b, sizeof(b), "%s: %d", TR("REKOR TERBAIK", "BEST SCORE"), S.best); textc(SCR_W / 2, 142, b, 1.3f, RGB(220, 220, 220));
    fmtM(earnedRun, m); snprintf(b, sizeof(b), "%s: %s", TR("UANG DIDAPAT", "EARNED"), m); textc(SCR_W / 2, 160, b, 1.3f, RGB(156, 204, 101));
    fmtM(S.money, m); snprintf(b, sizeof(b), "TOTAL: %s", m); textc(SCR_W / 2, 176, b, 1.3f, RGB(156, 204, 101));
    textc(SCR_W / 2, 200, TR("X: MAIN LAGI", "X: PLAY AGAIN"), 1.6f, RGB(90, 208, 111));
    textc(SCR_W / 2, 218, TR("O: GANTI MOBIL", "O: CHANGE CAR"), 1.6f, RGB(200, 200, 200));
}

/* ------------------------------------------------------------ update menu */
static void changeCar(int dlt) { ci = (ci + NCARS + dlt) % NCARS; pi = 0; showSc = .01f; }
static void startSpin(void) {
    if (spPhase) return;
    if (!canSpin()) { toast(TR("SPIN HARI INI SUDAH DIPAKAI", "TODAY'S SPIN ALREADY USED")); return; }
    float r = rnd() * 100; int i = r < 40 ? 0 : (r < 70 ? 1 : (r < 90 ? 2 : 3));
    const float lo[4] = {0, 40, 70, 90}, wd[4] = {40, 30, 20, 10};
    spRes = i; spTarget = lo[i] + wd[i] * (.15f + rnd() * .7f); spPhase = 1; spT = 0; spMsg[0] = 0;
}
static void finishSpin(void) {
    spPhase = 0; spMark = spTarget;
    if (spRes == 0) { S.money += 100000; snprintf(spMsg, sizeof(spMsg), "+RP 100.000"); }
    else if (spRes == 1) { S.mbat += 5; snprintf(spMsg, sizeof(spMsg), "+5 BATMAN COIN"); }
    else if (spRes == 2) snprintf(spMsg, sizeof(spMsg), "%s", TR("SPIN ULANG! PUTAR LAGI GRATIS", "SPIN AGAIN! FREE RE-SPIN"));
    else if (!S.owned[RBWI]) { S.owned[RBWI] = 1; snprintf(spMsg, sizeof(spMsg), "SECRET CAR! %s", TR("CEK DI GARASI", "CHECK THE GARAGE")); }
    else { S.money += 1000000; snprintf(spMsg, sizeof(spMsg), "%s +RP 1.000.000", TR("SECRET CAR GANDA", "DUPLICATE SECRET CAR")); }
    if (spRes != 2) S.lastSpin = todayNum();
    chkMs(); saveGame();
}
static void updateMenus(void) {
    int go = (pressed & PSP_CTRL_CROSS) != 0, back = (pressed & PSP_CTRL_CIRCLE) != 0;
    int lr = (pressed & PSP_CTRL_RIGHT) ? 1 : ((pressed & PSP_CTRL_LEFT) ? -1 : 0);
    int ud = (pressed & PSP_CTRL_DOWN) ? 1 : ((pressed & PSP_CTRL_UP) ? -1 : 0);
    switch (state) {
    case ST_INTRO: if (frame > 150 || go || (pressed & PSP_CTRL_START)) state = ST_LOBBY; break;
    case ST_LOBBY:
        selL = (selL + 6 + ud) % 6;
        if (go || lr) {
            if (selL == 0 && go) state = ST_GARAGE;
            else if (selL == 1 && go) state = ST_MIS;
            else if (selL == 2 && go) { state = ST_SPIN; }
            else if (selL == 3 && go) { state = ST_CODE; codeMsg[0] = 0; }
            else if (selL == 4) { WI = (WI + 4 + (lr ? lr : 1)) % 4; S.weather = WI; buildRoads(); saveGame(); }
            else if (selL == 5) { LANG = !LANG; S.lang = LANG; saveGame(); }
        }
        break;
    case ST_GARAGE:
        if (lr) changeCar(lr);
        if (ud && !(CARS[ci].secret && !S.owned[ci])) pi = (pi + 3 + ud) % 3;
        if (back) state = ST_LOBBY;
        if (go) {
            const CarDef *d = &CARS[ci];
            if (S.owned[ci]) startGame();
            else if (d->lim) { if (d->secret) toast(TR("SECRET CAR: MENANG DARI SPIN", "SECRET CAR: WIN IT FROM SPIN")); else state = ST_MIS; }
            else if (S.money >= d->price) { S.money -= d->price; S.owned[ci] = 1; chkMs(); saveGame(); toast(TR("MOBIL DIBELI!", "CAR PURCHASED!")); }
            else toast(TR("UANG KURANG", "NOT ENOUGH MONEY"));
        }
        if ((pressed & PSP_CTRL_SQUARE) && S.owned[ci]) {
            if (S.lv[ci] >= 10) toast(TR("LEVEL MAKS", "MAX LEVEL"));
            else if (S.money >= upCost(ci)) { S.money -= upCost(ci); S.lv[ci]++; saveGame(); toast(TR("UPGRADE BERHASIL!", "UPGRADED!")); }
            else toast(TR("UANG KURANG", "NOT ENOUGH MONEY"));
        }
        break;
    case ST_MIS:
        if (back) state = ST_LOBBY;
        if (go && msDone() && !S.owned[BATI]) { S.owned[BATI] = 1; saveGame(); ci = BATI; pi = 0; showSc = .01f; toast(TR("BATMAN LIMITED EDITION DIDAPAT!", "BATMAN LIMITED EDITION UNLOCKED!")); }
        break;
    case ST_SPIN:
        if (back && !spPhase) state = ST_LOBBY;
        if (go) startSpin();
        if (spPhase) { spT++; float u = 1 - spT / 90.0f; spMark = clampf(spTarget + 60 * u * u * cosf(u * 13), 0, 100); if (spT >= 90) finishSpin(); }
        break;
    case ST_CODE:
        if (back) state = ST_LOBBY;
        if (ud) codeCh[codePos] = (codeCh[codePos] + NCS - ud) % NCS;
        if (lr) codePos = (int)clampf((float)(codePos + lr), 0, 11);
        if (pressed & PSP_CTRL_SQUARE) { memset(codeCh, 0, sizeof(codeCh)); codePos = 0; codeMsg[0] = 0; }
        if (go) submitCode();
        break;
    case ST_OVER:
        if (go) startGame();
        if (back) { state = ST_GARAGE; }
        break;
    default: break;
    }
}

static void update(void) {
    for (int i = 0; i < 8; i++) { cloudX[i] += 2 * DT; if (cloudX[i] > 270) cloudX[i] = -270; }
    trafficUpdate();
    ang += DT * .8f; showSc += (1 - showSc) * minf(1, DT * 9);
    if (state == ST_PLAY) {
        updatePlay();
        if (state == ST_PLAY && (pressed & (PSP_CTRL_START | PSP_CTRL_TRIANGLE))) { finishRun(); state = ST_GARAGE; showSc = .01f; }
    } else updateMenus();
}

/* ------------------------------------------------------------ render */
static void render(void) {
    sceGuStart(GU_DIRECT, dlist);
    sceGuClearColor(WX[WI].bg); sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    if (state == ST_INTRO) {
        sceGuClearColor(0xFF1B0F05); sceGuClear(GU_COLOR_BUFFER_BIT);
        drawIntro(); sceGuFinish(); return;
    }
    if (WX[WI].night) { ui2dBegin(); for (int i = 0; i < 50; i++) rect(starX[i], starY[i], 2, 2, RGBA(255, 255, 255, 220)); }
    if (state == ST_PLAY || state == ST_OVER) {
        float fx = sinf(P.h), fz = cosf(P.h);
        scene3D(camx, camy, camz, P.x + fx * 4, 1.2f, P.z + fz * 4, camfov, 1);
    } else scene3D(0, 3.0f, 9.5f, 0, 1.2f, 0, 50, 0);
    ui2dBegin();
    if (WX[WI].ov) rect(0, 0, SCR_W, SCR_H, WX[WI].ov);
    drawWeatherFx();
    switch (state) {
    case ST_LOBBY: drawLobby(); break;
    case ST_GARAGE: drawGarage(); break;
    case ST_PLAY: hudDraw(); break;
    case ST_OVER: moneyBar(); drawOver(); break;
    case ST_MIS: moneyBar(); drawMis(); break;
    case ST_SPIN: moneyBar(); drawSpin(); break;
    case ST_CODE: moneyBar(); drawCode(); break;
    default: break;
    }
    drawToast();
    sceGuFinish();
}

/* ------------------------------------------------------------ init */
static void initGu(void) {
    void *fbp0 = (void *)0, *fbp1 = (void *)(BUF_W * SCR_H * 4), *zbp = (void *)(BUF_W * SCR_H * 4 * 2);
    sceGuInit(); sceGuStart(GU_DIRECT, dlist);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_W); sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_W); sceGuDepthBuffer(zbp, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2)); sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0); sceGuScissor(0, 0, SCR_W, SCR_H); sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL); sceGuEnable(GU_DEPTH_TEST); sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH); sceGuEnable(GU_CLIP_PLANES);
    sceGuFinish(); sceGuSync(0, 0); sceDisplayWaitVblankStart(); sceGuDisplay(GU_TRUE);
}
static int exitCb(int a1, int a2, void *c) { (void)a1; (void)a2; (void)c; sceKernelExitGame(); return 0; }
static int cbThread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int id = sceKernelCreateCallback("exit", exitCb, NULL); sceKernelRegisterExitCallback(id); sceKernelSleepThreadCB(); return 0;
}
static void setupCb(void) { int t = sceKernelCreateThread("cb", cbThread, 0x11, 0xFA0, 0, 0); if (t >= 0) sceKernelStartThread(t, 0, 0); }
static void readInput(void) {
    sceCtrlReadBufferPositive(&pad, 1);
    pressed = pad.Buttons & ~prevB; prevB = pad.Buttons;
    float ax = ((float)pad.Lx - 128.0f) / 128.0f; if (fabsf(ax) < .2f) ax = 0;
    axisX = -ax;
    if (pad.Buttons & PSP_CTRL_LEFT) axisX = 1;
    if (pad.Buttons & PSP_CTRL_RIGHT) axisX = -1;
}

int main(int argc, char **argv) {
    setupCb();
    scePowerSetClockFrequency(333, 333, 166);
    if (argc > 0 && argv && argv[0]) {
        char tmp[300]; strncpy(tmp, argv[0], sizeof(tmp) - 20); tmp[sizeof(tmp) - 20] = 0;
        char *sl = strrchr(tmp, '/');
        if (sl) { *(sl + 1) = 0; strcat(tmp, "save.dat"); strcpy(savePath, tmp); }
    }
    initGu();
    loadGame(); WI = S.weather & 3;
    initWorld(); initTraffic(); initParticles();
    for (int i = 0; i < 50; i++) { starX[i] = rnd() * SCR_W; starY[i] = rnd() * SCR_H * .5f; }
    for (int i = 0; i < 8; i++) cloudX[i] = -260 + i * 65.0f;
    sceCtrlSetSamplingCycle(0); sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    showSc = .01f;
    while (running) {
        readInput(); update(); render();
        sceGuSync(0, 0);
        sceDisplayWaitVblankStart(); sceDisplayWaitVblankStart(); /* 60Hz / 2 = 30 FPS */
        sceGuSwapBuffers();
        frame++;
    }
    sceGuTerm(); sceKernelExitGame();
    return 0;
}
