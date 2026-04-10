// EKG-EQ Standalone Renderer Test
// Renders the exact same GDI+ UI as the VST3 plugin, no host wrapper.

#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// ── Layout ────────────────────────────────────────────────────────────────
static const int EDITOR_W = 860;
static const int EDITOR_H = 480;
static const int COLS     = 6;
static const int COL_W    = EDITOR_W / COLS;
static const int HEADER_H = 36;
static const int DISP_Y   = HEADER_H;
static const int DISP_H   = 268;
static const int BAND_Y   = DISP_Y + DISP_H;
static const int BAR_Y    = EDITOR_H - 44;

// ── Coordinate helpers ────────────────────────────────────────────────────
static const double FMIN    = 20.0;
static const double FMAX    = 20000.0;
static const double DB_LO   = -24.0;
static const double DB_HI   =  24.0;
static const double LOGFMIN = 1.30103;
static const double LOGFRNG = 2.99957;

static Color gc(BYTE a, BYTE r, BYTE g, BYTE b) { return Color(a, r, g, b); }

static float fToX(double f) {
    return (float)((std::log10(f) - LOGFMIN) / LOGFRNG * EDITOR_W);
}
static float dbToY(double db) {
    db = std::max(DB_LO, std::min(DB_HI, db));
    return (float)(DISP_Y + (DB_HI - db) / (DB_HI - DB_LO) * DISP_H);
}

// ── Default params ────────────────────────────────────────────────────────
// normParam[band][0=gain, 1=freq, 2=Q]
// gain=0dB → norm 0.5;  freq default spreads per band;  Q=1.0 → norm ~0.051
static double normParams[6][3] = {
    { 0.5, 0.092, 0.051 },   // SUB:    0dB, ~60Hz,    Q≈1
    { 0.5, 0.183, 0.051 },   // BASS:   0dB, ~150Hz,   Q≈1
    { 0.5, 0.333, 0.051 },   // LO-MID: 0dB, ~500Hz,   Q≈1
    { 0.5, 0.475, 0.051 },   // MID:    0dB, ~1.5kHz,  Q≈1
    { 0.5, 0.583, 0.051 },   // HI-MID: 0dB, ~4.5kHz,  Q≈1
    { 0.5, 0.700, 0.033 },   // AIR:    0dB, ~12kHz,   Q≈0.71
};

static const BYTE BR[6] = { 230, 255, 140,  20,   0, 140 };
static const BYTE BG[6] = {  55, 150, 230, 205, 200, 100 };
static const BYTE BB[6] = {  55,   0,  30,  90, 230, 255 };

static Color bandGC(int b, BYTE a = 255) { return gc(a, BR[b], BG[b], BB[b]); }

static const wchar_t* BAND_NAMEW[6] = {L"SUB",L"BASS",L"LO-MID",L"MID",L"HI-MID",L"AIR"};

static double peakingDB(double dBgain, double f0, double Q, double f) {
    static const double FS = 48000.0, PI = 3.14159265358979323846;
    if (std::abs(dBgain) < 0.05) return 0.0;
    double A   = std::pow(10.0, dBgain / 40.0);
    double w0  = 2.0 * PI * f0 / FS;
    double sw0 = std::sin(w0), cw0 = std::cos(w0);
    double alp = sw0 / (2.0 * Q);
    if (alp < 1e-10) return 0.0;
    double ai = 1.0 / (1.0 + alp / A);
    double b0 = (1.0+alp*A)*ai, b1 = (-2.0*cw0)*ai, b2 = (1.0-alp*A)*ai;
    double a1 = (-2.0*cw0)*ai,  a2 = (1.0-alp/A)*ai;
    double wf = 2.0*PI*f/FS;
    double cf=std::cos(wf), sf=std::sin(wf), c2f=std::cos(2*wf), s2f=std::sin(2*wf);
    double nr=b0+b1*cf+b2*c2f, ni=-(b1*sf+b2*s2f);
    double dr=1.0+a1*cf+a2*c2f, di=-(a1*sf+a2*s2f);
    return 20.0*std::log10(std::sqrt((nr*nr+ni*ni)/(dr*dr+di*di+1e-30))+1e-30);
}

// ── Draw functions ────────────────────────────────────────────────────────
static void drawGlowCurve(Graphics& g, PointF* pts, int n, Color c, float cW) {
    struct P { BYTE a; float w; };
    P passes[] = { {5,cW*9},{15,cW*5},{50,cW*2.5f},{210,cW} };
    for (auto& p : passes) {
        Pen pen(gc(p.a, c.GetR(), c.GetG(), c.GetB()), p.w);
        pen.SetLineJoin(LineJoinRound);
        g.DrawCurve(&pen, pts, n, 0.4f);
    }
}

static void drawHeader(Graphics& g) {
    SolidBrush bg(gc(255,6,10,8));
    g.FillRectangle(&bg, 0, 0, EDITOR_W, HEADER_H);
    Pen sep(gc(120,0,160,100), 1.0f);
    g.DrawLine(&sep, 0, HEADER_H-1, EDITOR_W, HEADER_H-1);

    FontFamily ff(L"Courier New");
    StringFormat sf; sf.SetLineAlignment(StringAlignmentCenter);

    SolidBrush dotGreen(gc(255,0,200,120));
    g.FillEllipse(&dotGreen, 10.0f, (float)(HEADER_H/2)-4.0f, 8.0f, 8.0f);

    sf.SetAlignment(StringAlignmentNear);
    Font titleFont(&ff, 13.0f, FontStyleBold, UnitPixel);
    SolidBrush white(gc(230,210,230,220));
    RectF titleRect(24.0f, 0.0f, 120.0f, (float)HEADER_H);
    g.DrawString(L"EKG\u00B7EQ", -1, &titleFont, titleRect, &sf, &white);

    Font subFont(&ff, 8.0f, FontStyleRegular, UnitPixel);
    SolidBrush dim(gc(110,120,150,130));
    RectF subRect(140.0f, 0.0f, 440.0f, (float)HEADER_H);
    g.DrawString(L"MASTER EQ          WAVELET ANALYSIS", -1, &subFont, subRect, &sf, &dim);

    float sbW=52.0f, sbH=18.0f;
    float sbX=(float)(EDITOR_W-62), sbY=(float)(HEADER_H/2)-sbH/2;
    SolidBrush livePill(gc(255,0,155,75));
    g.FillRectangle(&livePill, sbX, sbY, sbW, sbH);
    Font sbFont(&ff, 8.0f, FontStyleBold, UnitPixel);
    SolidBrush sbText(gc(255,220,255,235));
    StringFormat ctr; ctr.SetAlignment(StringAlignmentCenter); ctr.SetLineAlignment(StringAlignmentCenter);
    RectF sbRect(sbX, sbY, sbW, sbH);
    g.DrawString(L"LIVE", -1, &sbFont, sbRect, &ctr, &sbText);
}

static void drawDisplay(Graphics& g) {
    const float X0=0.0f, Y0=(float)DISP_Y, W=(float)EDITOR_W, H=(float)DISP_H;

    SolidBrush dispBg(gc(255,3,12,7));
    g.FillRectangle(&dispBg, X0, Y0, W, H);

    // Grid
    Pen minorGrid(gc(28,0,90,55),1.0f), majorGrid(gc(50,0,115,70),1.0f), zeroLine(gc(80,0,150,90),1.0f);
    const int dbLines[]={-24,-18,-12,-6,6,12,18,24};
    for (int db:dbLines) { float y=dbToY(db); g.DrawLine((db%12==0)?&majorGrid:&minorGrid,X0,y,X0+W,y); }
    g.DrawLine(&zeroLine,X0,dbToY(0),X0+W,dbToY(0));
    Pen fMinor(gc(22,0,100,70),1.0f), fMajor(gc(48,0,130,90),1.0f);
    double minorF[]={30,40,50,60,70,80,90,200,300,400,500,600,700,800,900,2000,3000,4000,5000,6000,7000,8000,9000};
    for (double f:minorF) g.DrawLine(&fMinor,fToX(f),Y0,fToX(f),Y0+H);
    const double majF[]={100.0,1000.0,10000.0};
    for (double f:majF) g.DrawLine(&fMajor,fToX(f),Y0,fToX(f),Y0+H);

    // Spectrum silhouette
    const int SN=80;
    static const float AMP[SN]={
        0.62f,0.72f,0.78f,0.74f,0.68f,0.71f,0.65f,0.62f,
        0.58f,0.64f,0.70f,0.75f,0.68f,0.60f,0.55f,0.58f,
        0.63f,0.69f,0.72f,0.65f,0.59f,0.54f,0.57f,0.61f,
        0.67f,0.72f,0.66f,0.60f,0.55f,0.52f,0.50f,0.48f,
        0.46f,0.49f,0.53f,0.50f,0.46f,0.43f,0.40f,0.38f,
        0.36f,0.34f,0.37f,0.40f,0.37f,0.34f,0.31f,0.29f,
        0.27f,0.25f,0.28f,0.30f,0.27f,0.24f,0.22f,0.20f,
        0.18f,0.17f,0.19f,0.21f,0.18f,0.15f,0.13f,0.11f,
        0.10f,0.09f,0.10f,0.11f,0.09f,0.08f,0.07f,0.06f,
        0.05f,0.05f,0.04f,0.04f,0.03f,0.03f,0.02f,0.02f,
    };
    float floorY=Y0+H, specH=H*0.42f;
    for (int i=0;i<SN-1;++i) {
        double f1=FMIN*std::pow(FMAX/FMIN,(double)i/(SN-1));
        double f2=FMIN*std::pow(FMAX/FMIN,(double)(i+1)/(SN-1));
        float x1=fToX(f1), x2=fToX(f2);
        float amp=(AMP[i]+AMP[i+1])*0.5f;
        float h1=AMP[i]*specH, h2=AMP[i+1]*specH;
        BYTE r=(BYTE)(amp>0.55f?120+(BYTE)((amp-0.55f)*300):0);
        BYTE gv=(BYTE)(40+amp*160), bv=(BYTE)(amp*20), a=(BYTE)(18+amp*35);
        PointF poly[4]={{x1,floorY},{x1,floorY-h1-2},{x2,floorY-h2-2},{x2,floorY}};
        SolidBrush sb(gc(a,r,gv,bv));
        g.FillPolygon(&sb, poly, 4);
    }

    // EQ curves
    const int N=180;
    double totalDB[N]={}, bandDB[6][N]={};
    for (int b=0;b<6;++b) {
        double gainDB=normParams[b][0]*48.0-24.0;
        double f0=20.0*std::pow(1000.0,normParams[b][1]);
        double Q=0.1+normParams[b][2]*17.9;
        for (int i=0;i<N;++i) {
            double t=(double)i/(N-1), f=FMIN*std::pow(FMAX/FMIN,t);
            bandDB[b][i]=peakingDB(gainDB,f0,Q,f);
            totalDB[i]+=bandDB[b][i];
        }
    }
    PointF pts[N];
    for (int b=0;b<6;++b) {
        double gainDB=normParams[b][0]*48.0-24.0;
        if (std::abs(gainDB)<0.15) continue;
        for (int i=0;i<N;++i) {
            double t=(double)i/(N-1), f=FMIN*std::pow(FMAX/FMIN,t);
            pts[i]=PointF(fToX(f),dbToY(bandDB[b][i]));
        }
        drawGlowCurve(g, pts, N, bandGC(b), 1.6f);
    }
    for (int i=0;i<N;++i) {
        double t=(double)i/(N-1), f=FMIN*std::pow(FMAX/FMIN,t);
        pts[i]=PointF(fToX(f),dbToY(totalDB[i]));
    }
    drawGlowCurve(g, pts, N, gc(255,220,240,230), 1.1f);

    // Nodes
    for (int b=0;b<6;++b) {
        double gainDB=normParams[b][0]*48.0-24.0;
        double f0=20.0*std::pow(1000.0,normParams[b][1]);
        float nx=fToX(f0), ny=dbToY(gainDB);
        SolidBrush halo(gc(40,BR[b],BG[b],BB[b]));
        g.FillEllipse(&halo,nx-12.0f,ny-12.0f,24.0f,24.0f);
        Pen ring(gc(180,BR[b],BG[b],BB[b]),1.5f);
        g.DrawEllipse(&ring,nx-6.5f,ny-6.5f,13.0f,13.0f);
        SolidBrush core(gc(245,BR[b],BG[b],BB[b]));
        g.FillEllipse(&core,nx-3.5f,ny-3.5f,7.0f,7.0f);
    }

    // Labels
    FontFamily ff(L"Courier New");
    Font lblFont(&ff,8.0f,FontStyleRegular,UnitPixel);
    SolidBrush lblBrush(gc(70,0,140,85));
    StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter);
    struct { double f; const wchar_t* l; } fL[]={{50,L"50"},{90,L"90"},{200,L"200"},{500,L"500"},{1000,L"1k"},{5000,L"5k"},{10000,L"10k"},{20000,L"20k"}};
    for (auto& fl:fL) { float fx=fToX(fl.f); RectF lr(fx-18,Y0+H-14,36,13); g.DrawString(fl.l,-1,&lblFont,lr,&sfC,&lblBrush); }

    SolidBrush dbBrush(gc(60,0,130,80));
    StringFormat sfDB; sfDB.SetAlignment(StringAlignmentNear); sfDB.SetLineAlignment(StringAlignmentCenter);
    const int dbl[]={-24,-12,0,12,24};
    for (int db:dbl) { wchar_t buf[8]; swprintf_s(buf,L"%+d",db); float yd=dbToY(db); RectF lr(X0+3,yd-7,26,14); g.DrawString(buf,-1,&lblFont,lr,&sfDB,&dbBrush); }

    // Watermark
    Font wmFont(&ff,9.0f,FontStyleRegular,UnitPixel);
    SolidBrush wmBrush(gc(18,0,140,85));
    RectF wmRect(X0,Y0+H*0.5f-10,W,20);
    g.DrawString(L"STANDBY  |  ACTIVATE DETECTION  |  DOMAIN",-1,&wmFont,wmRect,&sfC,&wmBrush);
}

static void drawBandInfo(Graphics& g) {
    const float by=(float)BAND_Y, bh=(float)(BAR_Y-BAND_Y);
    SolidBrush bg(gc(255,5,8,6));
    g.FillRectangle(&bg,0.0f,by,(float)EDITOR_W,bh);
    Pen topRule(gc(60,0,130,80),1.0f);
    g.DrawLine(&topRule,0.0f,by,(float)EDITOR_W,by);

    FontFamily ff(L"Courier New");
    Font nameFont(&ff,9.5f,FontStyleBold,UnitPixel);
    Font bigFont(&ff,22.0f,FontStyleBold,UnitPixel);
    Font unitFont(&ff,8.5f,FontStyleRegular,UnitPixel);
    Font smFont(&ff,8.5f,FontStyleRegular,UnitPixel);
    Font tagFont(&ff,7.0f,FontStyleRegular,UnitPixel);
    SolidBrush dimGray(gc(90,80,100,90));
    StringFormat sfL; sfL.SetAlignment(StringAlignmentNear); sfL.SetLineAlignment(StringAlignmentNear);
    Pen colSep(gc(40,30,55,40),1.0f);

    for (int b=0;b<6;++b) {
        float x=(float)(b*COL_W), cw=(float)(COL_W-1);
        if (b>0) g.DrawLine(&colSep,x,by+1.0f,x,(float)(BAR_Y-1));
        double gainDB=normParams[b][0]*48.0-24.0;
        double freqHz=20.0*std::pow(1000.0,normParams[b][1]);
        double qVal=0.1+normParams[b][2]*17.9;
        SolidBrush bandBright(bandGC(b,220)), bandDim(bandGC(b,90)), labelGray(gc(80,100,130,110));
        float row=by+6.0f;

        SolidBrush dot(bandGC(b,220));
        g.FillEllipse(&dot,x+6.0f,row+3.0f,7.0f,7.0f);
        RectF nameRect(x+17.0f,row,cw-18.0f,14.0f);
        g.DrawString(BAND_NAMEW[b],-1,&nameFont,nameRect,&sfL,&bandBright);

        row+=18.0f;
        RectF gainLblRect(x+6.0f,row,32.0f,11.0f);
        g.DrawString(L"GAIN",-1,&tagFont,gainLblRect,&sfL,&labelGray);

        row+=10.0f;
        wchar_t gainBuf[12]; swprintf_s(gainBuf,L"%+.1f",gainDB);
        RectF bigRect(x+4.0f,row,cw-22.0f,28.0f);
        sfL.SetLineAlignment(StringAlignmentNear);
        g.DrawString(gainBuf,-1,&bigFont,bigRect,&sfL,&bandBright);
        RectF dbRect(x+cw-20.0f,row+2.0f,20.0f,14.0f);
        g.DrawString(L"dB",-1,&unitFont,dbRect,&sfL,&bandDim);

        row+=30.0f;
        wchar_t freqBuf[16];
        if (freqHz>=1000.0) swprintf_s(freqBuf,L"FREQ  %.1fk Hz",freqHz/1000.0);
        else                 swprintf_s(freqBuf,L"FREQ  %.0f Hz",freqHz);
        RectF fRect(x+6.0f,row,cw-8.0f,13.0f);
        g.DrawString(freqBuf,-1,&smFont,fRect,&sfL,&bandDim);

        row+=14.0f;
        wchar_t qBuf[12]; swprintf_s(qBuf,L"Q     %.2f",qVal);
        RectF qRect(x+6.0f,row,cw-8.0f,13.0f);
        g.DrawString(qBuf,-1,&smFont,qRect,&sfL,&dimGray);
    }
}

static void drawBottomBar(Graphics& g) {
    const float by=(float)BAR_Y, bh=(float)(EDITOR_H-BAR_Y), W=(float)EDITOR_W;
    SolidBrush bg(gc(255,4,8,5));
    g.FillRectangle(&bg,0.0f,by,W,bh);
    Pen topSep(gc(70,0,130,80),1.0f);
    g.DrawLine(&topSep,0.0f,by,W,by);

    FontFamily ff(L"Courier New");
    Font tagFont(&ff,7.0f,FontStyleRegular,UnitPixel);
    Font btnFont(&ff,8.5f,FontStyleRegular,UnitPixel);
    Font footFont(&ff,6.5f,FontStyleRegular,UnitPixel);
    SolidBrush dimGray(gc(65,80,110,85)), dimGreen(gc(75,0,140,85));
    StringFormat sfL; sfL.SetAlignment(StringAlignmentNear); sfL.SetLineAlignment(StringAlignmentCenter);
    StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);

    float meterY=by+bh*0.28f, meterH=bh*0.22f;
    RectF inLbl(8.0f,by+2.0f,46.0f,11.0f);
    g.DrawString(L"INPUT",-1,&tagFont,inLbl,&sfL,&dimGray);
    SolidBrush mBg(gc(255,6,18,10)), mFill(gc(90,0,155,85));
    Pen mBorder(gc(45,0,110,65),1.0f);
    g.FillRectangle(&mBg,8.0f,meterY,155.0f,meterH);
    g.FillRectangle(&mFill,8.0f,meterY,52.0f,meterH);
    g.DrawRectangle(&mBorder,8.0f,meterY,155.0f,meterH);
    RectF outLbl(172.0f,by+2.0f,52.0f,11.0f);
    g.DrawString(L"OUTPUT",-1,&tagFont,outLbl,&sfL,&dimGray);
    g.FillRectangle(&mBg,172.0f,meterY,155.0f,meterH);
    g.FillRectangle(&mFill,172.0f,meterY,52.0f,meterH);
    g.DrawRectangle(&mBorder,172.0f,meterY,155.0f,meterH);

    RectF footRect(0.0f,by+bh-12.0f,W,11.0f);
    g.DrawString(L"AURATONE TECHNOLOGY  \u2022  LUMINA CONSOLE",-1,&footFont,footRect,&sfC,&dimGreen);

    float btnY=by+5.0f, btnH=bh-11.0f;
    auto smallBtn=[&](float bx, float bw, const wchar_t* lbl){
        Pen border(gc(90,0,150,90),1.0f); g.DrawRectangle(&border,bx,btnY,bw,btnH);
        SolidBrush txt(gc(100,0,165,100)); RectF r(bx,btnY,bw,btnH);
        g.DrawString(lbl,-1,&btnFont,r,&sfC,&txt);
    };
    smallBtn(W-380.0f,68.0f,L"C-AUTO");
    smallBtn(W-304.0f,68.0f,L"B-CTRL");

    Pen bpBorder(gc(95,0,155,95),1.0f);
    g.DrawRectangle(&bpBorder,W-228.0f,btnY,68.0f,btnH);
    SolidBrush bpTxt(gc(95,0,155,95));
    RectF bpRect(W-228.0f,btnY,68.0f,btnH);
    g.DrawString(L"BYPASS",-1,&btnFont,bpRect,&sfC,&bpTxt);

    float dX=W-152.0f, dW=144.0f;
    SolidBrush dFill(gc(255,3,20,10)); g.FillRectangle(&dFill,dX,btnY,dW,btnH);
    Pen dBorder(gc(210,0,195,115),1.0f); g.DrawRectangle(&dBorder,dX,btnY,dW,btnH);
    SolidBrush dDot(gc(255,0,215,128));
    g.FillEllipse(&dDot,dX+10.0f,btnY+btnH/2.0f-4.5f,9.0f,9.0f);
    SolidBrush dTxt(gc(220,0,215,130));
    RectF dRect(dX+4.0f,btnY,dW-4.0f,btnH);
    g.DrawString(L"   DEACTIVATE",-1,&btnFont,dRect,&sfC,&dTxt);
}

// ── Paint everything ──────────────────────────────────────────────────────
static void paintAll(HDC hdc) {
    HDC     memDC  = CreateCompatibleDC(hdc);
    HBITMAP bmp    = CreateCompatibleBitmap(hdc, EDITOR_W, EDITOR_H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);
    {
        Graphics g(memDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        drawHeader(g);
        drawDisplay(g);
        drawBandInfo(g);
        drawBottomBar(g);
    }
    BitBlt(hdc, 0, 0, EDITOR_W, EDITOR_H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
}

// ── WinMain ───────────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            paintAll(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    ULONG_PTR gdiToken;
    GdiplusStartupInput gsi;
    GdiplusStartup(&gdiToken, &gsi, nullptr);

    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "EKGEQTest";
    RegisterClassExA(&wc);

    // Fixed size, non-resizable
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr = {0, 0, EDITOR_W, EDITOR_H};
    AdjustWindowRect(&wr, style, FALSE);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;

    HWND hwnd = CreateWindowExA(0, "EKGEQTest", "EKG-EQ Render Test",
        style, CW_USEDEFAULT, CW_USEDEFAULT, ww, wh,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiToken);
    return 0;
}
