#include "StdAfx.h"

///////////////////////////////////////////////////////////////////////////////////////
DECLARE_HANDLE(HZIP);	// An HZIP identifies a zip file that has been opened
typedef DWORD ZRESULT;
typedef struct
{ 
    int index;                 // index of this file within the zip
    char name[MAX_PATH];       // filename within the zip
    DWORD attr;                // attributes, as in GetFileAttributes.
    FILETIME atime,ctime,mtime;// access, create, modify filetimes
    long comp_size;            // sizes of item, compressed and uncompressed. These
    long unc_size;             // may be -1 if not yet known (e.g. being streamed in)
} ZIPENTRY;
typedef struct
{ 
    int index;                 // index of this file within the zip
    TCHAR name[MAX_PATH];      // filename within the zip
    DWORD attr;                // attributes, as in GetFileAttributes.
    FILETIME atime,ctime,mtime;// access, create, modify filetimes
    long comp_size;            // sizes of item, compressed and uncompressed. These
    long unc_size;             // may be -1 if not yet known (e.g. being streamed in)
} ZIPENTRYW;
#define OpenZip OpenZipU
#define CloseZip(hz) CloseZipU(hz)
extern HZIP OpenZipU(void *z,unsigned int len,DWORD flags);
extern ZRESULT CloseZipU(HZIP hz);
#ifdef _UNICODE
#define ZIPENTRY ZIPENTRYW
#define GetZipItem GetZipItemW
#define FindZipItem FindZipItemW
#else
#define GetZipItem GetZipItemA
#define FindZipItem FindZipItemA
#endif
extern ZRESULT GetZipItemA(HZIP hz, int index, ZIPENTRY *ze);
extern ZRESULT GetZipItemW(HZIP hz, int index, ZIPENTRYW *ze);
extern ZRESULT FindZipItemA(HZIP hz, const TCHAR *name, bool ic, int *index, ZIPENTRY *ze);
extern ZRESULT FindZipItemW(HZIP hz, const TCHAR *name, bool ic, int *index, ZIPENTRYW *ze);
extern ZRESULT UnzipItem(HZIP hz, int index, void *dst, unsigned int len, DWORD flags);
///////////////////////////////////////////////////////////////////////////////////////

extern "C"
{
    extern unsigned char *stbi_load_from_memory(unsigned char const *buffer, int len, int *x, int *y, \
        int *comp, int req_comp);
	extern void     stbi_image_free(void *retval_from_stbi_load);

};

namespace DuiLib {

/////////////////////////////////////////////////////////////////////////////////////
//
//

CRenderClip::~CRenderClip()
{
    ASSERT(::GetObjectType(hDC)==OBJ_DC || ::GetObjectType(hDC)==OBJ_MEMDC);
    ASSERT(::GetObjectType(hRgn)==OBJ_REGION);
    ASSERT(::GetObjectType(hOldRgn)==OBJ_REGION);
    ::SelectClipRgn(hDC, hOldRgn);
    ::DeleteObject(hOldRgn);
    ::DeleteObject(hRgn);
}

void CRenderClip::GenerateClip(HDC hDC, RECT rc, CRenderClip& clip)
{
    RECT rcClip = { 0 };
    ::GetClipBox(hDC, &rcClip);
    clip.hOldRgn = ::CreateRectRgnIndirect(&rcClip);
    clip.hRgn = ::CreateRectRgnIndirect(&rc);
#ifndef _WIN32_WCE
    ::ExtSelectClipRgn(hDC, clip.hRgn, RGN_AND);
#else
	::SelectClipRgn(hDC,clip.hRgn);
#endif
    clip.hDC = hDC;
    clip.rcItem = rc;
}

void CRenderClip::GenerateRoundClip(HDC hDC, RECT rc, RECT rcItem, int width, int height, CRenderClip& clip)
{
    RECT rcClip = { 0 };
    ::GetClipBox(hDC, &rcClip);
    clip.hOldRgn = ::CreateRectRgnIndirect(&rcClip);
    clip.hRgn = ::CreateRectRgnIndirect(&rc);
#ifndef _WIN32_WCE
    HRGN hRgnItem = ::CreateRoundRectRgn(rcItem.left, rcItem.top, rcItem.right + 1, rcItem.bottom + 1, width, height);
#else
	HRGN hRgnItem = ::CreateRectRgn(rcItem.left, rcItem.top, rcItem.right + 1, rcItem.bottom + 1);
#endif
    ::CombineRgn(clip.hRgn, clip.hRgn, hRgnItem, RGN_AND);
#ifndef _WIN32_WCE
    ::ExtSelectClipRgn(hDC, clip.hRgn, RGN_AND);
#else
	::SelectClipRgn(hDC,clip.hRgn);
#endif
    clip.hDC = hDC;
    clip.rcItem = rc;
    ::DeleteObject(hRgnItem);
}

void CRenderClip::UseOldClipBegin(HDC hDC, CRenderClip& clip)
{
    ::SelectClipRgn(hDC, clip.hOldRgn);
}

void CRenderClip::UseOldClipEnd(HDC hDC, CRenderClip& clip)
{
    ::SelectClipRgn(hDC, clip.hRgn);
}

/////////////////////////////////////////////////////////////////////////////////////
//
//

static const float OneThird = 1.0f / 3;

static void RGBtoHSL(DWORD ARGB, float* H, float* S, float* L) {
    const float
        R = (float)GetRValue(ARGB),
        G = (float)GetGValue(ARGB),
        B = (float)GetBValue(ARGB),
        nR = (R<0?0:(R>255?255:R))/255,
        nG = (G<0?0:(G>255?255:G))/255,
        nB = (B<0?0:(B>255?255:B))/255,
        m = min(min(nR,nG),nB),
        M = max(max(nR,nG),nB);
    *L = (m + M)/2;
    if (M==m) *H = *S = 0;
    else {
        const float
            f = (nR==m)?(nG-nB):((nG==m)?(nB-nR):(nR-nG)),
            i = (nR==m)?3.0f:((nG==m)?5.0f:1.0f);
        *H = (i-f/(M-m));
        if (*H>=6) *H-=6;
        *H*=60;
        *S = (2*(*L)<=1)?((M-m)/(M+m)):((M-m)/(2-M-m));
    }
}

static void HSLtoRGB(DWORD* ARGB, float H, float S, float L) {
    const float
        q = 2*L<1?L*(1+S):(L+S-L*S),
        p = 2*L-q,
        h = H/360,
        tr = h + OneThird,
        tg = h,
        tb = h - OneThird,
        ntr = tr<0?tr+1:(tr>1?tr-1:tr),
        ntg = tg<0?tg+1:(tg>1?tg-1:tg),
        ntb = tb<0?tb+1:(tb>1?tb-1:tb),
        R = 255*(6*ntr<1?p+(q-p)*6*ntr:(2*ntr<1?q:(3*ntr<2?p+(q-p)*6*(2.0f*OneThird-ntr):p))),
        G = 255*(6*ntg<1?p+(q-p)*6*ntg:(2*ntg<1?q:(3*ntg<2?p+(q-p)*6*(2.0f*OneThird-ntg):p))),
        B = 255*(6*ntb<1?p+(q-p)*6*ntb:(2*ntb<1?q:(3*ntb<2?p+(q-p)*6*(2.0f*OneThird-ntb):p)));
    *ARGB &= 0xFF000000;
    *ARGB |= RGB( (BYTE)(R<0?0:(R>255?255:R)), (BYTE)(G<0?0:(G>255?255:G)), (BYTE)(B<0?0:(B>255?255:B)) );
}

static COLORREF PixelAlpha(COLORREF clrSrc, double src_darken, COLORREF clrDest, double dest_darken)
{
    return RGB (GetRValue (clrSrc) * src_darken + GetRValue (clrDest) * dest_darken, 
        GetGValue (clrSrc) * src_darken + GetGValue (clrDest) * dest_darken, 
        GetBValue (clrSrc) * src_darken + GetBValue (clrDest) * dest_darken);

}

static BOOL WINAPI AlphaBitBlt(HDC hDC, int nDestX, int nDestY, int dwWidth, int dwHeight, HDC hSrcDC, \
                        int nSrcX, int nSrcY, int wSrc, int hSrc, BLENDFUNCTION ftn)
{
    HDC hTempDC = ::CreateCompatibleDC(hDC);
    if (NULL == hTempDC)
        return FALSE;

    //Creates Source DIB
#if ULIB_OPTIMIZE
	BITMAPINFO biSrc ={NULL};
	LPBITMAPINFO lpbiSrc = &biSrc;
	
#else
    LPBITMAPINFO lpbiSrc = NULL;
    // Fill in the BITMAPINFOHEADER

    lpbiSrc = (LPBITMAPINFO) new BYTE[sizeof(BITMAPINFOHEADER)];
	if (lpbiSrc == NULL)
	{
		::DeleteDC(hTempDC);
		return FALSE;
	}
#endif
	
    lpbiSrc->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    lpbiSrc->bmiHeader.biWidth = dwWidth;
    lpbiSrc->bmiHeader.biHeight = dwHeight;
    lpbiSrc->bmiHeader.biPlanes = 1;
    lpbiSrc->bmiHeader.biBitCount = 32;
    lpbiSrc->bmiHeader.biCompression = BI_RGB;
    lpbiSrc->bmiHeader.biSizeImage = dwWidth * dwHeight;
    lpbiSrc->bmiHeader.biXPelsPerMeter = 0;
    lpbiSrc->bmiHeader.biYPelsPerMeter = 0;
    lpbiSrc->bmiHeader.biClrUsed = 0;
    lpbiSrc->bmiHeader.biClrImportant = 0;

    COLORREF* pSrcBits = NULL;
    HBITMAP hSrcDib = CreateDIBSection (
        hSrcDC, lpbiSrc, DIB_RGB_COLORS, (void **)&pSrcBits,
        NULL, NULL);

    if ((NULL == hSrcDib) || (NULL == pSrcBits)) 
    {
#if ULIB_OPTIMIZE
#else
		delete [] lpbiSrc;
#endif
        ::DeleteDC(hTempDC);
        return FALSE;
    }

    HBITMAP hOldTempBmp = (HBITMAP)::SelectObject (hTempDC, hSrcDib);
    ::StretchBlt(hTempDC, 0, 0, dwWidth, dwHeight, hSrcDC, nSrcX, nSrcY, wSrc, hSrc, SRCCOPY);
    ::SelectObject (hTempDC, hOldTempBmp);
#if ULIB_OPTIMIZE
	BITMAPINFO biDest = {NULL};
	 LPBITMAPINFO lpbiDest =&biDest;
#else
    //Creates Destination DIB
    LPBITMAPINFO lpbiDest = NULL;
    // Fill in the BITMAPINFOHEADER
    lpbiDest = (LPBITMAPINFO) new BYTE[sizeof(BITMAPINFOHEADER)];
	if (lpbiDest == NULL)
	{
        delete [] lpbiSrc;
        ::DeleteObject(hSrcDib);
        ::DeleteDC(hTempDC);
        return FALSE;
	}
#endif
    lpbiDest->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    lpbiDest->bmiHeader.biWidth = dwWidth;
    lpbiDest->bmiHeader.biHeight = dwHeight;
    lpbiDest->bmiHeader.biPlanes = 1;
    lpbiDest->bmiHeader.biBitCount = 32;
    lpbiDest->bmiHeader.biCompression = BI_RGB;
    lpbiDest->bmiHeader.biSizeImage = dwWidth * dwHeight;
    lpbiDest->bmiHeader.biXPelsPerMeter = 0;
    lpbiDest->bmiHeader.biYPelsPerMeter = 0;
    lpbiDest->bmiHeader.biClrUsed = 0;
    lpbiDest->bmiHeader.biClrImportant = 0;

    COLORREF* pDestBits = NULL;
    HBITMAP hDestDib = CreateDIBSection (
        hDC, lpbiDest, DIB_RGB_COLORS, (void **)&pDestBits,
        NULL, NULL);

    if ((NULL == hDestDib) || (NULL == pDestBits))
    {
#if ULIB_OPTIMIZE
#else
        delete [] lpbiSrc;
#endif
        ::DeleteObject(hSrcDib);
        ::DeleteDC(hTempDC);
        return FALSE;
    }

    ::SelectObject (hTempDC, hDestDib);
    ::BitBlt (hTempDC, 0, 0, dwWidth, dwHeight, hDC, nDestX, nDestY, SRCCOPY);
    ::SelectObject (hTempDC, hOldTempBmp);

    double src_darken;
    BYTE nAlpha;

    for (int pixel = 0; pixel < dwWidth * dwHeight; pixel++, pSrcBits++, pDestBits++)
    {
        nAlpha = LOBYTE(*pSrcBits >> 24);
        src_darken = (double) (nAlpha * ftn.SourceConstantAlpha) / 255.0 / 255.0;
        if( src_darken < 0.0 ) src_darken = 0.0;
        *pDestBits = PixelAlpha(*pSrcBits, src_darken, *pDestBits, 1.0 - src_darken);
    } //for

    ::SelectObject (hTempDC, hDestDib);
    ::BitBlt (hDC, nDestX, nDestY, dwWidth, dwHeight, hTempDC, 0, 0, SRCCOPY);
    ::SelectObject (hTempDC, hOldTempBmp);
#if ULIB_OPTIMIZE
#else
    delete [] lpbiDest;
#endif
    ::DeleteObject(hDestDib);
#if ULIB_OPTIMIZE
#else
    delete [] lpbiSrc;
#endif
    ::DeleteObject(hSrcDib);

    ::DeleteDC(hTempDC);
    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////
//
//

DWORD CRenderEngine::AdjustColor(DWORD dwColor, short H, short S, short L)
{
    if( H == 180 && S == 100 && L == 100 ) return dwColor;
    float fH, fS, fL;
    float S1 = S / 100.0f;
    float L1 = L / 100.0f;
    RGBtoHSL(dwColor, &fH, &fS, &fL);
    fH += (H - 180);
    fH = fH > 0 ? fH : fH + 360; 
    fS *= S1;
    fL *= L1;
    HSLtoRGB(&dwColor, fH, fS, fL);
    return dwColor;
}

TImageInfo* CRenderEngine::LoadImage(STRINGorID bitmap, LPCTSTR type, DWORD mask)
{
    LPBYTE pData = NULL;
    DWORD dwSize = 0;

    if( type == NULL ) {
        CStdString sFile = CPaintManagerUI::GetResourcePath();
        if( CPaintManagerUI::GetResourceZip().IsEmpty() ) {
            sFile += bitmap.m_lpstr;
            HANDLE hFile = ::CreateFile(sFile.GetData(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, \
                FILE_ATTRIBUTE_NORMAL, NULL);
            if( hFile == INVALID_HANDLE_VALUE ) return NULL;
            dwSize = ::GetFileSize(hFile, NULL);
            if( dwSize == 0 ) return NULL;

            DWORD dwRead = 0;
            pData = new BYTE[ dwSize ];
            ::ReadFile( hFile, pData, dwSize, &dwRead, NULL );
            ::CloseHandle( hFile );

            if( dwRead != dwSize ) {
                delete[] pData;
                return NULL;
            }
        }
        else {
            sFile += CPaintManagerUI::GetResourceZip();
            HZIP hz = NULL;
            if( CPaintManagerUI::IsCachedResourceZip() ) hz = (HZIP)CPaintManagerUI::GetResourceZipHandle();
            else hz = OpenZip((void*)sFile.GetData(), 0, 2);
            if( hz == NULL ) return NULL;
            ZIPENTRY ze; 
            int i; 
            if( FindZipItem(hz, bitmap.m_lpstr, true, &i, &ze) != 0 ) return NULL;
            dwSize = ze.unc_size;
            if( dwSize == 0 ) return NULL;
            pData = new BYTE[ dwSize ];
            int res = UnzipItem(hz, i, pData, dwSize, 3);
            if( res != 0x00000000 && res != 0x00000600) {
                delete[] pData;
                if( !CPaintManagerUI::IsCachedResourceZip() ) CloseZip(hz);
                return NULL;
            }
            if( !CPaintManagerUI::IsCachedResourceZip() ) CloseZip(hz);
        }
    }
    else {
        HRSRC hResource = ::FindResource(CPaintManagerUI::GetResourceDll(), bitmap.m_lpstr, type);
        if( hResource == NULL ) return NULL;
        HGLOBAL hGlobal = ::LoadResource(CPaintManagerUI::GetResourceDll(), hResource);
        if( hGlobal == NULL ) {
#ifndef _WIN32_WCE
            FreeResource(hResource);
#endif
            return NULL;
        }

        dwSize = ::SizeofResource(CPaintManagerUI::GetResourceDll(), hResource);
        if( dwSize == 0 ) return NULL;
        pData = new BYTE[ dwSize ];
        ::CopyMemory(pData, (LPBYTE)::LockResource(hGlobal), dwSize);
#ifndef _WIN32_WCE
        ::FreeResource(hResource);
#endif
    }

    LPBYTE pImage = NULL;
    int x,y,n;
    pImage = stbi_load_from_memory(pData, dwSize, &x, &y, &n, 4);
    delete[] pData;
    if( !pImage ) return NULL;

    BITMAPINFO bmi;
    ::ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = x;
    bmi.bmiHeader.biHeight = -y;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = x * y * 4;

    bool bAlphaChannel = false;
    LPBYTE pDest = NULL;
    HBITMAP hBitmap = ::CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void**)&pDest, NULL, 0);
    if( !hBitmap ) return NULL;

    for( int i = 0; i < x * y; i++ ) 
    {
        pDest[i*4 + 3] = pImage[i*4 + 3];
        if( pDest[i*4 + 3] < 255 )
        {
            pDest[i*4] = (BYTE)(DWORD(pImage[i*4 + 2])*pImage[i*4 + 3]/255);
            pDest[i*4 + 1] = (BYTE)(DWORD(pImage[i*4 + 1])*pImage[i*4 + 3]/255);
            pDest[i*4 + 2] = (BYTE)(DWORD(pImage[i*4])*pImage[i*4 + 3]/255); 
            bAlphaChannel = true;
        }
        else
        {
            pDest[i*4] = pImage[i*4 + 2];
            pDest[i*4 + 1] = pImage[i*4 + 1];
            pDest[i*4 + 2] = pImage[i*4]; 
        }

        if( *(DWORD*)(&pDest[i*4]) == mask ) {
            pDest[i*4] = (BYTE)0;
            pDest[i*4 + 1] = (BYTE)0;
            pDest[i*4 + 2] = (BYTE)0; 
            pDest[i*4 + 3] = (BYTE)0;
            bAlphaChannel = true;
        }
    }

    stbi_image_free(pImage);

    TImageInfo* data = new TImageInfo;
    data->hBitmap = hBitmap;
    data->nX = x;
    data->nY = y;
    data->alphaChannel = bAlphaChannel;
    return data;
}
//需要显示区域
static void PaintImgChange(RECT* pPaint,RECT* pDest,RECT* pImg)
{
	int nHeight=0;//高度差
	if(pPaint->bottom==pDest->bottom)return;
	if(pPaint->top==pDest->top)
	{
		nHeight=pDest->bottom-pPaint->bottom;
		pImg->bottom-=nHeight;
	}
	else
	{
		nHeight=pPaint->top-pDest->top;
		pImg->top+=nHeight;
		pImg->bottom-=nHeight;
	}
}
void CRenderEngine::DrawImage(HDC hDC, HBITMAP hBitmap, const RECT& rc, const RECT& rcPaint,
                                    const RECT& rcBmpPart, const RECT& rcCorners, bool alphaChannel, 
                                    BYTE uFade, bool hole, bool xtiled, bool ytiled)
{
    ASSERT(::GetObjectType(hDC)==OBJ_DC || ::GetObjectType(hDC)==OBJ_MEMDC);

    typedef BOOL (WINAPI *LPALPHABLEND)(HDC, int, int, int, int,HDC, int, int, int, int, BLENDFUNCTION);
#ifndef _WIN32_WCE
    static LPALPHABLEND lpAlphaBlend = (LPALPHABLEND) ::GetProcAddress(::GetModuleHandle(_T("msimg32.dll")), "AlphaBlend");
#else
	static LPALPHABLEND lpAlphaBlend = (LPALPHABLEND) ::GetProcAddress(::GetModuleHandle(_T("Coredll.dll")), TEXT("AlphaBlend"));
#endif
    if( lpAlphaBlend == NULL ) lpAlphaBlend = AlphaBitBlt;
    if( hBitmap == NULL ) return;

    HDC hCloneDC = ::CreateCompatibleDC(hDC);
    HBITMAP hOldBitmap = (HBITMAP) ::SelectObject(hCloneDC, hBitmap);
    ::SetStretchBltMode(hDC, COLORONCOLOR);

    RECT rcTemp = {0};
    RECT rcDest = {0};
	RECT rcImg={0};
    if( lpAlphaBlend && (alphaChannel || uFade < 255) ) {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, uFade, AC_SRC_ALPHA };
        // middle
        if( !hole ) {
            rcDest.left = rc.left + rcCorners.left;
            rcDest.top = rc.top + rcCorners.top;
            rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
            rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;
            if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                if( !xtiled && !ytiled ) {
					rcDest.right -= rcDest.left;
					rcDest.bottom -= rcDest.top;
					rcTemp.right -= rcTemp.left;
					rcTemp.bottom -= rcTemp.top;
					rcImg.left=rcBmpPart.left + rcCorners.left;
					rcImg.top= rcBmpPart.top + rcCorners.top;
					rcImg.right=rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right;
					rcImg.bottom=rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom;
					PaintImgChange(&rcTemp,&rcDest,&rcImg);
					lpAlphaBlend(hDC, rcTemp.left, rcTemp.top, rcTemp.right, rcTemp.bottom, hCloneDC, \
						rcImg.left,rcImg.top, \
						rcImg.right, \
						rcImg.bottom, bf);
                }
                else if( xtiled && ytiled ) {
                    LONG lWidth = rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right;
                    LONG lHeight = rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom;
                    int iTimesX = (rcDest.right - rcDest.left + lWidth - 1) / lWidth;
                    int iTimesY = (rcDest.bottom - rcDest.top + lHeight - 1) / lHeight;
                    for( int j = 0; j < iTimesY; ++j ) {
                        LONG lDestTop = rcDest.top + lHeight * j;
                        LONG lDestBottom = rcDest.top + lHeight * (j + 1);
                        LONG lDrawHeight = lHeight;
                        if( lDestBottom > rcDest.bottom ) {
                            lDrawHeight -= lDestBottom - rcDest.bottom;
                            lDestBottom = rcDest.bottom;
                        }
                        for( int i = 0; i < iTimesX; ++i ) {
                            LONG lDestLeft = rcDest.left + lWidth * i;
                            LONG lDestRight = rcDest.left + lWidth * (i + 1);
                            LONG lDrawWidth = lWidth;
                            if( lDestRight > rcDest.right ) {
                                lDrawWidth -= lDestRight - rcDest.right;
                                lDestRight = rcDest.right;
                            }
                            lpAlphaBlend(hDC, rcDest.left + lWidth * i, rcDest.top + lHeight * j, 
                                lDestRight - lDestLeft, lDestBottom - lDestTop, hCloneDC, 
                                rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, lDrawWidth, lDrawHeight, bf);
                        }
                    }
                }
                else if( xtiled ) {
                    LONG lWidth = rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right;
                    int iTimes = (rcDest.right - rcDest.left + lWidth - 1) / lWidth;
                    for( int i = 0; i < iTimes; ++i ) {
                        LONG lDestLeft = rcDest.left + lWidth * i;
                        LONG lDestRight = rcDest.left + lWidth * (i + 1);
                        LONG lDrawWidth = lWidth;
                        if( lDestRight > rcDest.right ) {
                            lDrawWidth -= lDestRight - rcDest.right;
                            lDestRight = rcDest.right;
                        }
                        lpAlphaBlend(hDC, lDestLeft, rcDest.top, lDestRight - lDestLeft, rcDest.bottom, 
                            hCloneDC, rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, \
                            lDrawWidth, rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom, bf);
                    }
                }
                else { // ytiled
                    LONG lHeight = rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom;
                    int iTimes = (rcDest.bottom - rcDest.top + lHeight - 1) / lHeight;
                    for( int i = 0; i < iTimes; ++i ) {
                        LONG lDestTop = rcDest.top + lHeight * i;
                        LONG lDestBottom = rcDest.top + lHeight * (i + 1);
                        LONG lDrawHeight = lHeight;
                        if( lDestBottom > rcDest.bottom ) {
                            lDrawHeight -= lDestBottom - rcDest.bottom;
                            lDestBottom = rcDest.bottom;
                        }
                        lpAlphaBlend(hDC, rcDest.left, rcDest.top + lHeight * i, rcDest.right, lDestBottom - lDestTop, 
                            hCloneDC, rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, \
                            rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right, lDrawHeight, bf);                    
                    }
                }
            }
        }

        // left-top
        if( rcCorners.left > 0 && rcCorners.top > 0 ) {
            rcDest.left = rc.left;
            rcDest.top = rc.top;
            rcDest.right = rcCorners.left;
            rcDest.bottom = rcCorners.top;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;
            if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                    rcBmpPart.left, rcBmpPart.top, rcCorners.left, rcCorners.top, bf);
            }
        }
        // top
        if( rcCorners.top > 0 ) {
            rcDest.left = rc.left + rcCorners.left;
            rcDest.top = rc.top;
            rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
            rcDest.bottom = rcCorners.top;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;
            if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                    rcBmpPart.left + rcCorners.left, rcBmpPart.top, rcBmpPart.right - rcBmpPart.left - \
                    rcCorners.left - rcCorners.right, rcCorners.top, bf);
            }
        }
        // right-top
        if( rcCorners.right > 0 && rcCorners.top > 0 ) {
            rcDest.left = rc.right - rcCorners.right;
            rcDest.top = rc.top;
            rcDest.right = rcCorners.right;
            rcDest.bottom = rcCorners.top;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;
            if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                    rcBmpPart.right - rcCorners.right, rcBmpPart.top, rcCorners.right, rcCorners.top, bf);
            }
        }
        // left
        if( rcCorners.left > 0 ) {
            rcDest.left = rc.left;
            rcDest.top = rc.top + rcCorners.top;
            rcDest.right = rcCorners.left;
            rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;
            if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                    rcBmpPart.left, rcBmpPart.top + rcCorners.top, rcCorners.left, rcBmpPart.bottom - \
                    rcBmpPart.top - rcCorners.top - rcCorners.bottom, bf);
            }
        }
        // right
        if( rcCorners.right > 0 ) {
            rcDest.left = rc.right - rcCorners.right;
            rcDest.top = rc.top + rcCorners.top;
            rcDest.right = rcCorners.right;
            rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;
            if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                    rcBmpPart.right - rcCorners.right, rcBmpPart.top + rcCorners.top, rcCorners.right, \
                    rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom, bf);
            }
        }
        // left-bottom
        if( rcCorners.left > 0 && rcCorners.bottom > 0 ) {
            rcDest.left = rc.left;
            rcDest.top = rc.bottom - rcCorners.bottom;
            rcDest.right = rcCorners.left;
            rcDest.bottom = rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;
            if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                    rcBmpPart.left, rcBmpPart.bottom - rcCorners.bottom, rcCorners.left, rcCorners.bottom, bf);
            }
        }
        // bottom
        if( rcCorners.bottom > 0 ) {
            rcDest.left = rc.left + rcCorners.left;
            rcDest.top = rc.bottom - rcCorners.bottom;
            rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
            rcDest.bottom = rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;
            if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                    rcBmpPart.left + rcCorners.left, rcBmpPart.bottom - rcCorners.bottom, \
                    rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right, rcCorners.bottom, bf);
            }
        }
        // right-bottom
        if( rcCorners.right > 0 && rcCorners.bottom > 0 ) {
            rcDest.left = rc.right - rcCorners.right;
            rcDest.top = rc.bottom - rcCorners.bottom;
            rcDest.right = rcCorners.right;
            rcDest.bottom = rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;
            if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                    rcBmpPart.right - rcCorners.right, rcBmpPart.bottom - rcCorners.bottom, rcCorners.right, \
                    rcCorners.bottom, bf);
            }
        }
    }
    else 
    {
        if (rc.right - rc.left == rcBmpPart.right - rcBmpPart.left \
            && rc.bottom - rc.top == rcBmpPart.bottom - rcBmpPart.top \
            && rcCorners.left == 0 && rcCorners.right == 0 && rcCorners.top == 0 && rcCorners.bottom == 0)
        {
            if( ::IntersectRect(&rcTemp, &rcPaint, &rc) ) {
                ::BitBlt(hDC, rcTemp.left, rcTemp.top, rcTemp.right - rcTemp.left, rcTemp.bottom - rcTemp.top, \
                    hCloneDC, rcBmpPart.left + rcTemp.left - rc.left, rcBmpPart.top + rcTemp.top - rc.top, SRCCOPY);
            }
        }
        else
        {
            // middle
            if( !hole ) {
                rcDest.left = rc.left + rcCorners.left;
                rcDest.top = rc.top + rcCorners.top;
                rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
                rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;
                if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                    if( !xtiled && !ytiled ) {
                        rcDest.right -= rcDest.left;
                        rcDest.bottom -= rcDest.top;
                        ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                            rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, \
                            rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right, \
                            rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom, SRCCOPY);
                    }
                    else if( xtiled && ytiled ) {
                        LONG lWidth = rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right;
                        LONG lHeight = rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom;
                        int iTimesX = (rcDest.right - rcDest.left + lWidth - 1) / lWidth;
                        int iTimesY = (rcDest.bottom - rcDest.top + lHeight - 1) / lHeight;
                        for( int j = 0; j < iTimesY; ++j ) {
                            LONG lDestTop = rcDest.top + lHeight * j;
                            LONG lDestBottom = rcDest.top + lHeight * (j + 1);
                            LONG lDrawHeight = lHeight;
                            if( lDestBottom > rcDest.bottom ) {
                                lDrawHeight -= lDestBottom - rcDest.bottom;
                                lDestBottom = rcDest.bottom;
                            }
                            for( int i = 0; i < iTimesX; ++i ) {
                                LONG lDestLeft = rcDest.left + lWidth * i;
                                LONG lDestRight = rcDest.left + lWidth * (i + 1);
                                LONG lDrawWidth = lWidth;
                                if( lDestRight > rcDest.right ) {
                                    lDrawWidth -= lDestRight - rcDest.right;
                                    lDestRight = rcDest.right;
                                }
                                ::BitBlt(hDC, rcDest.left + lWidth * i, rcDest.top + lHeight * j, \
                                    lDestRight - lDestLeft, lDestBottom - lDestTop, hCloneDC, \
                                    rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, SRCCOPY);
                            }
                        }
                    }
                    else if( xtiled ) {
                        LONG lWidth = rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right;
                        int iTimes = (rcDest.right - rcDest.left + lWidth - 1) / lWidth;
                        for( int i = 0; i < iTimes; ++i ) {
                            LONG lDestLeft = rcDest.left + lWidth * i;
                            LONG lDestRight = rcDest.left + lWidth * (i + 1);
                            LONG lDrawWidth = lWidth;
                            if( lDestRight > rcDest.right ) {
                                lDrawWidth -= lDestRight - rcDest.right;
                                lDestRight = rcDest.right;
                            }
                            ::StretchBlt(hDC, lDestLeft, rcDest.top, lDestRight - lDestLeft, rcDest.bottom, 
                                hCloneDC, rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, \
                                lDrawWidth, rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom, SRCCOPY);
                        }
                    }
                    else { // ytiled
                        LONG lHeight = rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom;
                        int iTimes = (rcDest.bottom - rcDest.top + lHeight - 1) / lHeight;
                        for( int i = 0; i < iTimes; ++i ) {
                            LONG lDestTop = rcDest.top + lHeight * i;
                            LONG lDestBottom = rcDest.top + lHeight * (i + 1);
                            LONG lDrawHeight = lHeight;
                            if( lDestBottom > rcDest.bottom ) {
                                lDrawHeight -= lDestBottom - rcDest.bottom;
                                lDestBottom = rcDest.bottom;
                            }
                            ::StretchBlt(hDC, rcDest.left, rcDest.top + lHeight * i, rcDest.right, lDestBottom - lDestTop, 
                                hCloneDC, rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, \
                                rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right, lDrawHeight, SRCCOPY);                    
                        }
                    }
                }
            }
            
            // left-top
            if( rcCorners.left > 0 && rcCorners.top > 0 ) {
                rcDest.left = rc.left;
                rcDest.top = rc.top;
                rcDest.right = rcCorners.left;
                rcDest.bottom = rcCorners.top;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;
                if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                        rcBmpPart.left, rcBmpPart.top, rcCorners.left, rcCorners.top, SRCCOPY);
                }
            }
            // top
            if( rcCorners.top > 0 ) {
                rcDest.left = rc.left + rcCorners.left;
                rcDest.top = rc.top;
                rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
                rcDest.bottom = rcCorners.top;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;
                if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                        rcBmpPart.left + rcCorners.left, rcBmpPart.top, rcBmpPart.right - rcBmpPart.left - \
                        rcCorners.left - rcCorners.right, rcCorners.top, SRCCOPY);
                }
            }
            // right-top
            if( rcCorners.right > 0 && rcCorners.top > 0 ) {
                rcDest.left = rc.right - rcCorners.right;
                rcDest.top = rc.top;
                rcDest.right = rcCorners.right;
                rcDest.bottom = rcCorners.top;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;
                if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                        rcBmpPart.right - rcCorners.right, rcBmpPart.top, rcCorners.right, rcCorners.top, SRCCOPY);
                }
            }
            // left
            if( rcCorners.left > 0 ) {
                rcDest.left = rc.left;
                rcDest.top = rc.top + rcCorners.top;
                rcDest.right = rcCorners.left;
                rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;
                if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                        rcBmpPart.left, rcBmpPart.top + rcCorners.top, rcCorners.left, rcBmpPart.bottom - \
                        rcBmpPart.top - rcCorners.top - rcCorners.bottom, SRCCOPY);
                }
            }
            // right
            if( rcCorners.right > 0 ) {
                rcDest.left = rc.right - rcCorners.right;
                rcDest.top = rc.top + rcCorners.top;
                rcDest.right = rcCorners.right;
                rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;
                if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                        rcBmpPart.right - rcCorners.right, rcBmpPart.top + rcCorners.top, rcCorners.right, \
                        rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom, SRCCOPY);
                }
            }
            // left-bottom
            if( rcCorners.left > 0 && rcCorners.bottom > 0 ) {
                rcDest.left = rc.left;
                rcDest.top = rc.bottom - rcCorners.bottom;
                rcDest.right = rcCorners.left;
                rcDest.bottom = rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;
                if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                        rcBmpPart.left, rcBmpPart.bottom - rcCorners.bottom, rcCorners.left, rcCorners.bottom, SRCCOPY);
                }
            }
            // bottom
            if( rcCorners.bottom > 0 ) {
                rcDest.left = rc.left + rcCorners.left;
                rcDest.top = rc.bottom - rcCorners.bottom;
                rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
                rcDest.bottom = rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;
                if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                        rcBmpPart.left + rcCorners.left, rcBmpPart.bottom - rcCorners.bottom, \
                        rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right, rcCorners.bottom, SRCCOPY);
                }
            }
            // right-bottom
            if( rcCorners.right > 0 && rcCorners.bottom > 0 ) {
                rcDest.left = rc.right - rcCorners.right;
                rcDest.top = rc.bottom - rcCorners.bottom;
                rcDest.right = rcCorners.right;
                rcDest.bottom = rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;
                if( ::IntersectRect(&rcTemp, &rcPaint, &rcDest) ) {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                        rcBmpPart.right - rcCorners.right, rcBmpPart.bottom - rcCorners.bottom, rcCorners.right, \
                        rcCorners.bottom, SRCCOPY);
                }
            }
        }
    }

    ::SelectObject(hCloneDC, hOldBitmap);
    ::DeleteDC(hCloneDC);
}

////////////////////////////////////////////////////////////////
//modify by weiny
void notStretchRect(BYTE &nAlign,RECT& rcItem,int &nWidth,int &nHeight,int nStretchWidth,int nStretchHeight)
{
	//nomal=0,center=1,left=2,right=3,top=4,bottom=5
	if(//(nAlign==0||nAlign==2)&&
		nStretchWidth<nWidth)
		rcItem.right=rcItem.left+nStretchWidth;
	if(//(nAlign==0)&&
		nStretchHeight<nHeight)
		rcItem.bottom=rcItem.top+nStretchHeight;

	if((nAlign==1||nAlign==4||nAlign==5)&&
		nStretchWidth<nWidth
		)
	{
		rcItem.left+=(nWidth-nStretchWidth)/2;
		rcItem.right=rcItem.left+nStretchWidth;}
	if((nAlign==1||nAlign==2||nAlign==3)&&
		nStretchHeight<nHeight
		)
	{
		rcItem.top+=(nHeight-nStretchHeight)/2;
		rcItem.bottom=rcItem.top+nStretchHeight;}
	if((nAlign==3)&&nStretchWidth<nWidth)
		rcItem.right=rcItem.left+(nWidth)-nStretchWidth;
	if(nAlign==5&&nStretchHeight<nHeight)
		rcItem.bottom=rcItem.top+(nHeight)-nStretchHeight;
}
void equalStretchRect(BYTE &nAlign,RECT& rcItem,int &nWidth,int &nHeight,const TImageInfo* data)
{
	int nStretchWidth=0,nStretchHeight=0;
	double p1=(double)data->nX/data->nY;
	double p2=(double)nWidth/nHeight;
	if(p1==p2) return ;

	if(p1>p2) {//比较高宽比例，确定以宽或者是高为基准进行计算。
		//if(data->nX>nWidth){//以宽为基准开始计算，
		//当宽度大于限定宽度，开始缩放
		nStretchWidth=nWidth;
		nStretchHeight=nStretchWidth/p1;
#if 0
	}
	else{
		//当宽度小于限定宽度，直接返回原始数值。
		nStretchWidth=data->nX;
		nStretchHeight=data->nY;
	}
#endif
}
	else if(p1<p2)
	{
		//if(data->nY>nHeight){//以高为基准，进行计算
		//当高度大于限定高度，开始缩放。
		nStretchHeight=nHeight;
		nStretchWidth=nStretchHeight*p1;
#if 0
		}else{
			//当高度小于限定高度，直接返回原始数值。
			nStretchWidth=data->nX;
			nStretchHeight=data->nY;
		}
#endif
	}
	notStretchRect(nAlign,rcItem,nWidth,nHeight,nStretchWidth,nStretchHeight);
}
void StretchRect(BYTE nAlign,BYTE nStretch,RECT& rcItem,const TImageInfo* data)
{
	if(nStretch!=0&&nStretch!=2)return;
	int nWidth=rcItem.right-rcItem.left;
	int nHeight=rcItem.bottom-rcItem.top;
	switch (nStretch)
	{
	case 0://不拉伸
		notStretchRect(nAlign,rcItem,nWidth,nHeight,data->nX,data->nY);
		return ;
	case 2://等比拉伸
		equalStretchRect(nAlign,rcItem,nWidth,nHeight,data);return;
	default:
		return;
	}
}
//////////////////////////////////////////////////////////////
bool DrawImage(HDC hDC, CPaintManagerUI* pManager, const RECT& rc, const RECT& rcPaint, const CStdString& sImageName, \
		const CStdString& sImageResType, RECT rcItem, RECT rcBmpPart, RECT rcCorner, DWORD dwMask, BYTE bFade, \
		bool bHole, bool bTiledX, bool bTiledY,BYTE nAlign,BYTE nStretch)
{
	const TImageInfo* data = NULL;
	if( sImageResType.IsEmpty() ) {
		data = pManager->GetImageEx((LPCTSTR)sImageName, NULL, dwMask);
	}
	else {
		data = pManager->GetImageEx((LPCTSTR)sImageName, (LPCTSTR)sImageResType, dwMask);
	}
	if( !data ) return false;    
	//////////////////////////////////////////
	//modify by weiny
	StretchRect(nAlign,nStretch,rcItem,data);
	////////////////////////////////////////////
	if( rcBmpPart.left == 0 && rcBmpPart.right == 0 && rcBmpPart.top == 0 && rcBmpPart.bottom == 0 ) {
		rcBmpPart.right = data->nX;
		rcBmpPart.bottom = data->nY;
	}
	if (rcBmpPart.right > data->nX) rcBmpPart.right = data->nX;
	if (rcBmpPart.bottom > data->nY) rcBmpPart.bottom = data->nY;

	RECT rcTemp;
	if( !::IntersectRect(&rcTemp, &rcItem, &rc) ) return true;
	if( !::IntersectRect(&rcTemp, &rcItem, &rcPaint) ) return true;

	CRenderEngine::DrawImage(hDC, data->hBitmap, rcItem, rcPaint, rcBmpPart, rcCorner, data->alphaChannel, bFade, bHole, bTiledX, bTiledY);

	return true;
}

bool CRenderEngine::DrawImageString(HDC hDC, CPaintManagerUI* pManager, const RECT& rc, const RECT& rcPaint, 
                                          LPCTSTR pStrImage, LPCTSTR pStrModify)
{
	if ((pManager == NULL) || (hDC == NULL)) return false;

    // 1、aaa.jpg
    // 2、file='aaa.jpg' res='' restype='0' dest='0,0,0,0' source='0,0,0,0' corner='0,0,0,0' 
    // mask='#FF0000' fade='255' hole='false' xtiled='false' ytiled='false'

    CStdString sImageName = pStrImage;
    CStdString sImageResType;
    RECT rcItem = rc;
    RECT rcBmpPart = {0};
    RECT rcCorner = {0};
    DWORD dwMask = 0;
    BYTE bFade = 0xFF;
    bool bHole = false;
    bool bTiledX = false;
    bool bTiledY = false;

	int image_count = 0;
	//////////////////////////////////////////////////////////////////////////
	//stretch 取值 1占满,2等比拉伸,0不拉伸
	//align 取值 nomal=0,center=1,left=2,right=3,top=4,bottom=5
	//modify by weiny
	//////////////////////////////////////////////////////////////////////////
	BYTE nStretch=0x1;
	BYTE nAlign=0x0;
    CStdString sItem;
    CStdString sValue;
    LPTSTR pstr = NULL;

    for( int i = 0; i < 2; ++i,image_count = 0 ) {
        if( i == 1)
            pStrImage = pStrModify;

        if( !pStrImage ) continue;

        while( *pStrImage != _T('\0') ) {
            sItem.Empty();
            sValue.Empty();
            while( *pStrImage > _T('\0') && *pStrImage <= _T(' ') ) pStrImage = ::CharNext(pStrImage);
            while( *pStrImage != _T('\0') && *pStrImage != _T('=') && *pStrImage > _T(' ') ) {
                LPTSTR pstrTemp = ::CharNext(pStrImage);
                while( pStrImage < pstrTemp) {
                    sItem += *pStrImage++;
                }
            }
            while( *pStrImage > _T('\0') && *pStrImage <= _T(' ') ) pStrImage = ::CharNext(pStrImage);
            if( *pStrImage++ != _T('=') ) break;
            while( *pStrImage > _T('\0') && *pStrImage <= _T(' ') ) pStrImage = ::CharNext(pStrImage);
            if( *pStrImage++ != _T('\'') ) break;
            while( *pStrImage != _T('\0') && *pStrImage != _T('\'') ) {
                LPTSTR pstrTemp = ::CharNext(pStrImage);
                while( pStrImage < pstrTemp) {
                    sValue += *pStrImage++;
                }
            }
            if( *pStrImage++ != _T('\'') ) break;
            if( !sValue.IsEmpty() ) {
				switch(sItem.GetAt(0))
				{
				case 'a':
					//modify by weiny
					if(sItem==_T("align")){
						switch(sValue.GetAt(0))
						{
						case 'c':
							nAlign=0x1;break;
						case 'l':
							nAlign=0x2;break;
						case 'r':
							nAlign=0x3;break;
						case 't':
							nAlign=0x4;break;
						case 'b':
							nAlign=0x5;break;
						default:
							nAlign=0x0;break;
						}
					}
					break;
				case 'c':
					if( sItem == _T("corner") ) 
					{
						rcCorner.left = _tcstol(sValue.GetData(), &pstr, 10);  ASSERT(pstr);    
						rcCorner.top = _tcstol(pstr + 1, &pstr, 10);    ASSERT(pstr);    
						rcCorner.right = _tcstol(pstr + 1, &pstr, 10);  ASSERT(pstr);    
						rcCorner.bottom = _tcstol(pstr + 1, &pstr, 10); ASSERT(pstr);
					}
					break;
				case 'd':
					if( sItem == _T("dest") ) {
						rcItem.left = rc.left + _tcstol(sValue.GetData(), &pstr, 10);  ASSERT(pstr);    
						rcItem.top = rc.top + _tcstol(pstr + 1, &pstr, 10);    ASSERT(pstr);
						rcItem.right = rc.left + _tcstol(pstr + 1, &pstr, 10);  ASSERT(pstr);
						if (rcItem.right > rc.right) rcItem.right = rc.right;
						rcItem.bottom = rc.top + _tcstol(pstr + 1, &pstr, 10); ASSERT(pstr);
						if (rcItem.bottom > rc.bottom) rcItem.bottom = rc.bottom;
					}break;
				
				case 'h':
					if( sItem == _T("hole") ) {
						bHole = (_tcscmp(sValue.GetData(), _T("true")) == 0);
					}break;
				case 's':
					if( sItem == _T("source") ) {
						rcBmpPart.left = _tcstol(sValue.GetData(), &pstr, 10);  ASSERT(pstr);    
						rcBmpPart.top = _tcstol(pstr + 1, &pstr, 10);    ASSERT(pstr);    
						rcBmpPart.right = _tcstol(pstr + 1, &pstr, 10);  ASSERT(pstr);    
						rcBmpPart.bottom = _tcstol(pstr + 1, &pstr, 10); ASSERT(pstr);  
					}
					else if(sItem==_T("stretch")){
						nStretch=_tcstoul(sValue.GetData(), &pstr, 10);
					}
					break;

				case 'm':
					if( sItem == _T("mask") ) {
						if( sValue[0] == _T('#')) dwMask = _tcstoul(sValue.GetData() + 1, &pstr, 16);
						else dwMask = _tcstoul(sValue.GetData(), &pstr, 16);
					}break;
				case 'r':
					if( sItem == _T("restype") ) {
						if( image_count > 0 )
							DuiLib::DrawImage(hDC, pManager, rc, rcPaint, sImageName, sImageResType,
							rcItem, rcBmpPart, rcCorner, dwMask, bFade, bHole, bTiledX, bTiledY,nAlign,nStretch);

						sImageResType = sValue;
						++image_count;
						break;
					}
				case 'f':
					if( sItem == _T("file") || sItem == _T("res") ) {
						if( image_count > 0 )
							DuiLib::DrawImage(hDC, pManager, rc, rcPaint, sImageName, sImageResType,
							rcItem, rcBmpPart, rcCorner, dwMask, bFade, bHole, bTiledX, bTiledY,nAlign,nStretch);

						sImageName = sValue;
						if( sItem == _T("file") )
							++image_count;
					}
					else if( sItem == _T("fade") ) {
						bFade = (BYTE)_tcstoul(sValue.GetData(), &pstr, 10);
					}break;
					break;
				
				case 'x':
					if( sItem == _T("xtiled") ) {
						bTiledX = (_tcscmp(sValue.GetData(), _T("true")) == 0);
					}break;
				case 'y':
					if( sItem == _T("ytiled") ) {
						bTiledY = (_tcscmp(sValue.GetData(), _T("true")) == 0);
					}break;
				}
            }
            if( *pStrImage++ != _T(' ') ) break;
        }
    }

	DuiLib::DrawImage(hDC, pManager, rc, rcPaint, sImageName, sImageResType,
		rcItem, rcBmpPart, rcCorner, dwMask, bFade, bHole, bTiledX, bTiledY,nAlign,nStretch);

    return true;
}

void CRenderEngine::DrawColor(HDC hDC, const RECT& rc, DWORD color)
{
    if( color <= 0x00FFFFFF ) return;
    if( color >= 0xFF000000 )
    {
        ::SetBkColor(hDC, RGB(GetBValue(color), GetGValue(color), GetRValue(color)));
        ::ExtTextOut(hDC, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
    }
    else
    {
        // Create a new 32bpp bitmap with room for an alpha channel
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 1;
        bmi.bmiHeader.biHeight = 1;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biSizeImage = 1 * 1 * sizeof(DWORD);
        LPDWORD pDest = NULL;
        HBITMAP hBitmap = ::CreateDIBSection(hDC, &bmi, DIB_RGB_COLORS, (LPVOID*) &pDest, NULL, 0);
        if( !hBitmap ) return;

        *pDest = color;

        RECT rcBmpPart = {0, 0, 1, 1};
        RECT rcCorners = {0};
        DrawImage(hDC, hBitmap, rc, rc, rcBmpPart, rcCorners, true, 255);
        ::DeleteObject(hBitmap);
    }
}

void CRenderEngine::DrawGradient(HDC hDC, const RECT& rc, DWORD dwFirst, DWORD dwSecond, bool bVertical, int nSteps)
{
    typedef BOOL (WINAPI *LPALPHABLEND)(HDC, int, int, int, int,HDC, int, int, int, int, BLENDFUNCTION);
#ifndef _WIN32_WCE
    static LPALPHABLEND lpAlphaBlend = (LPALPHABLEND) ::GetProcAddress(::GetModuleHandle(_T("msimg32.dll")), "AlphaBlend");
#else
	static LPALPHABLEND lpAlphaBlend =(LPALPHABLEND) ::GetProcAddress(::GetModuleHandle(_T("Coredll.dll")), TEXT("AlphaBlend"));
#endif
    if( lpAlphaBlend == NULL ) lpAlphaBlend = AlphaBitBlt;
    typedef BOOL (WINAPI *PGradientFill)(HDC, PTRIVERTEX, ULONG, PVOID, ULONG, ULONG);
#ifndef _WIN32_WCE
    static PGradientFill lpGradientFill = (PGradientFill) ::GetProcAddress(::GetModuleHandle(_T("msimg32.dll")), "GradientFill");
#else
	static PGradientFill lpGradientFill = GradientFill;//(PGradientFill) ::GetProcAddress(::GetModuleHandle(_T("msimg32.dll")), TEXT("GradientFill"));
#endif
    BYTE bAlpha = (BYTE)(((dwFirst >> 24) + (dwSecond >> 24)) >> 1);
    if( bAlpha == 0 ) return;
    int cx = rc.right - rc.left;
    int cy = rc.bottom - rc.top;
    RECT rcPaint = rc;
    HDC hPaintDC = hDC;
    HBITMAP hPaintBitmap = NULL;
    HBITMAP hOldPaintBitmap = NULL;
    if( bAlpha < 255 ) {
        rcPaint.left = rcPaint.top = 0;
        rcPaint.right = cx;
        rcPaint.bottom = cy;
        hPaintDC = ::CreateCompatibleDC(hDC);
        hPaintBitmap = ::CreateCompatibleBitmap(hDC, cx, cy);
        ASSERT(hPaintDC);
        ASSERT(hPaintBitmap);
        hOldPaintBitmap = (HBITMAP) ::SelectObject(hPaintDC, hPaintBitmap);
    }
    if( lpGradientFill != NULL ) 
    {
        TRIVERTEX triv[2] = 
        {
            { rcPaint.left, rcPaint.top, GetBValue(dwFirst) << 8, GetGValue(dwFirst) << 8, GetRValue(dwFirst) << 8, 0xFF00 },
            { rcPaint.right, rcPaint.bottom, GetBValue(dwSecond) << 8, GetGValue(dwSecond) << 8, GetRValue(dwSecond) << 8, 0xFF00 }
        };
        GRADIENT_RECT grc = { 0, 1 };
        lpGradientFill(hPaintDC, triv, 2, &grc, 1, bVertical ? GRADIENT_FILL_RECT_V : GRADIENT_FILL_RECT_H);
    }
    else 
    {
        // Determine how many shades
        int nShift = 1;
        if( nSteps >= 64 ) nShift = 6;
        else if( nSteps >= 32 ) nShift = 5;
        else if( nSteps >= 16 ) nShift = 4;
        else if( nSteps >= 8 ) nShift = 3;
        else if( nSteps >= 4 ) nShift = 2;
        int nLines = 1 << nShift;
        for( int i = 0; i < nLines; i++ ) {
            // Do a little alpha blending
            BYTE bR = (BYTE) ((GetBValue(dwSecond) * (nLines - i) + GetBValue(dwFirst) * i) >> nShift);
            BYTE bG = (BYTE) ((GetGValue(dwSecond) * (nLines - i) + GetGValue(dwFirst) * i) >> nShift);
            BYTE bB = (BYTE) ((GetRValue(dwSecond) * (nLines - i) + GetRValue(dwFirst) * i) >> nShift);
            // ... then paint with the resulting color
            HBRUSH hBrush = ::CreateSolidBrush(RGB(bR,bG,bB));
            RECT r2 = rcPaint;
            if( bVertical ) {
                r2.bottom = rc.bottom - ((i * (rc.bottom - rc.top)) >> nShift);
                r2.top = rc.bottom - (((i + 1) * (rc.bottom - rc.top)) >> nShift);
                if( (r2.bottom - r2.top) > 0 ) ::FillRect(hDC, &r2, hBrush);
            }
            else {
                r2.left = rc.right - (((i + 1) * (rc.right - rc.left)) >> nShift);
                r2.right = rc.right - ((i * (rc.right - rc.left)) >> nShift);
                if( (r2.right - r2.left) > 0 ) ::FillRect(hPaintDC, &r2, hBrush);
            }
            ::DeleteObject(hBrush);
        }
    }
    if( bAlpha < 255 ) {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, bAlpha, AC_SRC_ALPHA };
        lpAlphaBlend(hDC, rc.left, rc.top, cx, cy, hPaintDC, 0, 0, cx, cy, bf);
        ::SelectObject(hPaintDC, hOldPaintBitmap);
        ::DeleteObject(hPaintBitmap);
        ::DeleteDC(hPaintDC);
    }
}

void CRenderEngine::DrawLine(HDC hDC, const RECT& rc, int nSize, DWORD dwPenColor)
{
    ASSERT(::GetObjectType(hDC)==OBJ_DC || ::GetObjectType(hDC)==OBJ_MEMDC);
    POINT ptTemp = { 0 };
    HPEN hPen = ::CreatePen(PS_SOLID, nSize, RGB(GetBValue(dwPenColor), GetGValue(dwPenColor), GetRValue(dwPenColor)));
    HPEN hOldPen = (HPEN)::SelectObject(hDC, hPen);
    ::MoveToEx(hDC, rc.left, rc.top, &ptTemp);
    ::LineTo(hDC, rc.right, rc.bottom);
    ::SelectObject(hDC, hOldPen);
    ::DeleteObject(hPen);
}

void CRenderEngine::DrawRect(HDC hDC, const RECT& rc, int nSize, DWORD dwPenColor)
{
    ASSERT(::GetObjectType(hDC)==OBJ_DC || ::GetObjectType(hDC)==OBJ_MEMDC);
#ifndef _WIN32_WCE
    HPEN hPen = ::CreatePen(PS_SOLID | PS_INSIDEFRAME, nSize, RGB(GetBValue(dwPenColor), GetGValue(dwPenColor), GetRValue(dwPenColor)));
#else
	HPEN hPen = ::CreatePen(PS_SOLID , nSize, RGB(GetBValue(dwPenColor), GetGValue(dwPenColor), GetRValue(dwPenColor)));
#endif
    HPEN hOldPen = (HPEN)::SelectObject(hDC, hPen);
    ::SelectObject(hDC, ::GetStockObject(HOLLOW_BRUSH));
    ::Rectangle(hDC, rc.left, rc.top, rc.right, rc.bottom);
    ::SelectObject(hDC, hOldPen);
    ::DeleteObject(hPen);
}

void CRenderEngine::DrawRoundRect(HDC hDC, const RECT& rc, int nSize, int width, int height, DWORD dwPenColor)
{
    ASSERT(::GetObjectType(hDC)==OBJ_DC || ::GetObjectType(hDC)==OBJ_MEMDC);
#ifndef _WIN32_WCE
    HPEN hPen = ::CreatePen(PS_SOLID | PS_INSIDEFRAME, nSize, RGB(GetBValue(dwPenColor), GetGValue(dwPenColor), GetRValue(dwPenColor)));
#else
	HPEN hPen = ::CreatePen(PS_SOLID , nSize, RGB(GetBValue(dwPenColor), GetGValue(dwPenColor), GetRValue(dwPenColor)));
#endif
    HPEN hOldPen = (HPEN)::SelectObject(hDC, hPen);
    ::SelectObject(hDC, ::GetStockObject(HOLLOW_BRUSH));
    ::RoundRect(hDC, rc.left, rc.top, rc.right, rc.bottom, width, height);
    ::SelectObject(hDC, hOldPen);
    ::DeleteObject(hPen);
}

void CRenderEngine::DrawText(HDC hDC, CPaintManagerUI* pManager, RECT& rc, LPCTSTR pstrText, DWORD dwTextColor, int iFont, UINT uStyle)
{
    ASSERT(::GetObjectType(hDC)==OBJ_DC || ::GetObjectType(hDC)==OBJ_MEMDC);
    if( pstrText == NULL || pManager == NULL ) return;
    ::SetBkMode(hDC, TRANSPARENT);
    ::SetTextColor(hDC, RGB(GetBValue(dwTextColor), GetGValue(dwTextColor), GetRValue(dwTextColor)));
    HFONT hOldFont = (HFONT)::SelectObject(hDC, pManager->GetFont(iFont));
    ::DrawText(hDC, pstrText, -1, &rc, uStyle | DT_NOPREFIX);
    ::SelectObject(hDC, hOldFont);
}

void CRenderEngine::DrawHtmlText(HDC hDC, CPaintManagerUI* pManager, RECT& rc, LPCTSTR pstrText, DWORD dwTextColor, RECT* prcLinks, CStdString* sLinks, int& nLinkRects, UINT uStyle,RECT* pRect)
{
    // 考虑到在xml编辑器中使用<>符号不方便，可以使用{}符号代替
    // 支持标签嵌套（如<l><b>text</b></l>），但是交叉嵌套是应该避免的（如<l><b>text</l></b>）
    // The string formatter supports a kind of "mini-html" that consists of various short tags:
    //
    //   Bold:             <b>text</b>
    //   Color:            <c #xxxxxx>text</c>  where x = RGB in hex
    //   Font:             <f x>text</f>        where x = font id
    //   Italic:           <i>text</i>
    //   Image:            <i x y z>            where x = image name and y = imagelist num and z(optional) = imagelist id
    //   Link:             <a x>text</a>        where x(optional) = link content, normal like app:notepad or http:www.xxx.com
    //   NewLine           <n>                  
    //   Paragraph:        <p x>text</p>        where x = extra pixels indent in p
    //   Raw Text:         <r>text</r>
    //   Selected:         <s>text</s>
    //   Underline:        <u>text</u>
    //   X Indent:         <x i>                where i = hor indent in pixels
    //   Y Indent:         <y i>                where i = ver indent in pixels 

    ASSERT(::GetObjectType(hDC)==OBJ_DC || ::GetObjectType(hDC)==OBJ_MEMDC);
    if( pstrText == NULL || pManager == NULL ) return;
    if( ::IsRectEmpty(&rc) ) return;

    bool bDraw = (uStyle & DT_CALCRECT) == 0;

    CStdPtrArray aFontArray(10);
    CStdPtrArray aColorArray(10);
    CStdPtrArray aPIndentArray(10);

    RECT rcClip = { 0 };
    ::GetClipBox(hDC, &rcClip);
    HRGN hOldRgn = ::CreateRectRgnIndirect(&rcClip);
    HRGN hRgn = ::CreateRectRgnIndirect(&rc);
#ifndef _WIN32_WCE
    if( bDraw ) ::ExtSelectClipRgn(hDC, hRgn, RGN_AND);
#else
	if(bDraw) ::SelectClipRgn(hDC,hRgn);
#endif
    TEXTMETRIC* pTm = &pManager->GetDefaultFontInfo()->tm;
    HFONT hOldFont = (HFONT) ::SelectObject(hDC, pManager->GetDefaultFontInfo()->hFont);
    ::SetBkMode(hDC, TRANSPARENT);
    ::SetTextColor(hDC, RGB(GetBValue(dwTextColor), GetGValue(dwTextColor), GetRValue(dwTextColor)));
    DWORD dwBkColor = pManager->GetDefaultSelectedBkColor();
    ::SetBkColor(hDC, RGB(GetBValue(dwBkColor), GetGValue(dwBkColor), GetRValue(dwBkColor)));

    // If the drawstyle include a alignment, we'll need to first determine the text-size so
    // we can draw it at the correct position...
    if( (uStyle & DT_SINGLELINE) != 0 && (uStyle & DT_VCENTER) != 0 && (uStyle & DT_CALCRECT) == 0 ) {
        RECT rcText = { 0, 0, 9999, 100 };
        int nLinks = 0;
        DrawHtmlText(hDC, pManager, rcText, pstrText, dwTextColor, NULL, NULL, nLinks, uStyle | DT_CALCRECT,pRect);
        rc.top = rc.top + ((rc.bottom - rc.top) / 2) - ((rcText.bottom - rcText.top) / 2);
        rc.bottom = rc.top + (rcText.bottom - rcText.top);
    }
    if( (uStyle & DT_SINGLELINE) != 0 && (uStyle & DT_CENTER) != 0 && (uStyle & DT_CALCRECT) == 0 ) {
        RECT rcText = { 0, 0, 9999, 100 };
        int nLinks = 0;
        DrawHtmlText(hDC, pManager, rcText, pstrText, dwTextColor, NULL, NULL, nLinks, uStyle | DT_CALCRECT,pRect);
        rc.left = rc.left + ((rc.right - rc.left) / 2) - ((rcText.right - rcText.left) / 2);
        rc.right = rc.left + (rcText.right - rcText.left);
    }
    if( (uStyle & DT_SINGLELINE) != 0 && (uStyle & DT_RIGHT) != 0 && (uStyle & DT_CALCRECT) == 0 ) {
        RECT rcText = { 0, 0, 9999, 100 };
        int nLinks = 0;
        DrawHtmlText(hDC, pManager, rcText, pstrText, dwTextColor, NULL, NULL, nLinks, uStyle | DT_CALCRECT,pRect);
        rc.left = rc.right - (rcText.right - rcText.left);
    }

    bool bHoverLink = false;
    CStdString sHoverLink;
    POINT ptMouse = pManager->GetMousePos();
    for( int i = 0; !bHoverLink && i < nLinkRects; i++ ) {
        if( ::PtInRect(prcLinks + i, ptMouse) ) {
            sHoverLink = *(CStdString*)(sLinks + i);
            bHoverLink = true;
        }
    }

    POINT pt = { rc.left, rc.top };
    int iLinkIndex = 0;
    int cyLine = pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1);
    int cyMinHeight = 0;
    int cxMaxWidth = 0;
    POINT ptLinkStart = { 0 };
    bool bLineEnd = false;
    bool bInRaw = false;
    bool bInLink = false;
    bool bInSelected = false;
    int iLineLinkIndex = 0;

    // 排版习惯是图文底部对齐，所以每行绘制都要分两步，先计算高度，再绘制
    CStdPtrArray aLineFontArray;
    CStdPtrArray aLineColorArray;
    CStdPtrArray aLinePIndentArray;
    LPCTSTR pstrLineBegin = pstrText;
    bool bLineInRaw = false;
    bool bLineInLink = false;
    bool bLineInSelected = false;
    int cyLineHeight = 0;
    bool bLineDraw = false; // 行的第二阶段：绘制
    while( *pstrText != _T('\0') ) {
        if( pt.x >= rc.right || *pstrText == _T('\n') || bLineEnd ) {
            if( *pstrText == _T('\n') ) pstrText++;
            if( bLineEnd ) bLineEnd = false;
            if( !bLineDraw ) {
                if( bInLink && iLinkIndex < nLinkRects ) {
                    ::SetRect(&prcLinks[iLinkIndex++], ptLinkStart.x, ptLinkStart.y, MIN(pt.x, rc.right), pt.y + cyLine);
                    CStdString *pStr1 = (CStdString*)(sLinks + iLinkIndex - 1);
                    CStdString *pStr2 = (CStdString*)(sLinks + iLinkIndex);
                    *pStr2 = *pStr1;
                }
                for( int i = iLineLinkIndex; i < iLinkIndex; i++ ) {
                    prcLinks[i].bottom = pt.y + cyLine;
                }
                if( bDraw ) {
                    bInLink = bLineInLink;
                    iLinkIndex = iLineLinkIndex;
                }
            }
            else {
                if( bInLink && iLinkIndex < nLinkRects ) iLinkIndex++;
                bLineInLink = bInLink;
                iLineLinkIndex = iLinkIndex;
            }
            if( (uStyle & DT_SINGLELINE) != 0 && (!bDraw || bLineDraw) ) break;
            if( bDraw ) bLineDraw = !bLineDraw; // !
            pt.x = rc.left;
            if( !bLineDraw ) pt.y += cyLine;
            if( pt.y > rc.bottom && bDraw ) break;
            ptLinkStart = pt;
            cyLine = pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1);
            if( pt.x >= rc.right ) break;
        }
        else if( !bInRaw && ( *pstrText == _T('<') || *pstrText == _T('{') )
            && ( pstrText[1] >= _T('a') && pstrText[1] <= _T('z') )
            && ( pstrText[2] == _T(' ') || pstrText[2] == _T('>') || pstrText[2] == _T('}') ) ) {
                pstrText++;
                LPCTSTR pstrNextStart = NULL;
                switch( *pstrText ) {
            case _T('a'):  // Link
                {
                    pstrText++;
                    while( *pstrText > _T('\0') && *pstrText <= _T(' ') ) pstrText = ::CharNext(pstrText);
                    if( iLinkIndex < nLinkRects && !bLineDraw ) {
                        CStdString *pStr = (CStdString*)(sLinks + iLinkIndex);
                        pStr->Empty();
                        while( *pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}') ) {
                            LPCTSTR pstrTemp = ::CharNext(pstrText);
                            while( pstrText < pstrTemp) {
                                *pStr += *pstrText++;
                            }
                        }
                    }

                    DWORD clrColor = pManager->GetDefaultLinkFontColor();
                    if( bHoverLink && iLinkIndex < nLinkRects ) {
                        CStdString *pStr = (CStdString*)(sLinks + iLinkIndex);
                        if( sHoverLink == *pStr ) clrColor = pManager->GetDefaultLinkHoverFontColor();
                    }
                    //else if( prcLinks == NULL ) {
                    //    if( ::PtInRect(&rc, ptMouse) )
                    //        clrColor = pManager->GetDefaultLinkHoverFontColor();
                    //}
                    aColorArray.Add((LPVOID)clrColor);
                    ::SetTextColor(hDC,  RGB(GetBValue(clrColor), GetGValue(clrColor), GetRValue(clrColor)));
                    TFontInfo* pFontInfo = pManager->GetDefaultFontInfo();
                    if( aFontArray.GetSize() > 0 ) pFontInfo = (TFontInfo*)aFontArray.GetAt(aFontArray.GetSize() - 1);
                    if( pFontInfo->bUnderline == false ) {
                        HFONT hFont = pManager->GetFont(pFontInfo->sFontName, pFontInfo->iSize, pFontInfo->bBold, true, pFontInfo->bItalic);
                        if( hFont == NULL ) hFont = pManager->AddFont(pFontInfo->sFontName, pFontInfo->iSize, pFontInfo->bBold, true, pFontInfo->bItalic);
                        pFontInfo = pManager->GetFontInfo(hFont);
                        aFontArray.Add(pFontInfo);
                        pTm = &pFontInfo->tm;
                        ::SelectObject(hDC, pFontInfo->hFont);
                        cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
                    }
                    ptLinkStart = pt;
                    bInLink = true;
                }
                break;
            case _T('b'):  // Bold
                {
                    pstrText++;
                    TFontInfo* pFontInfo = pManager->GetDefaultFontInfo();
                    if( aFontArray.GetSize() > 0 ) pFontInfo = (TFontInfo*)aFontArray.GetAt(aFontArray.GetSize() - 1);
                    if( pFontInfo->bBold == false ) {
                        HFONT hFont = pManager->GetFont(pFontInfo->sFontName, pFontInfo->iSize, true, pFontInfo->bUnderline, pFontInfo->bItalic);
                        if( hFont == NULL ) hFont = pManager->AddFont(pFontInfo->sFontName, pFontInfo->iSize, true, pFontInfo->bUnderline, pFontInfo->bItalic);
                        pFontInfo = pManager->GetFontInfo(hFont);
                        aFontArray.Add(pFontInfo);
                        pTm = &pFontInfo->tm;
                        ::SelectObject(hDC, pFontInfo->hFont);
                        cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
                    }
                }
                break;
            case _T('c'):  // Color
                {
                    pstrText++;
                    while( *pstrText > _T('\0') && *pstrText <= _T(' ') ) pstrText = ::CharNext(pstrText);
                    if( *pstrText == _T('#')) pstrText++;
                    DWORD clrColor = _tcstol(pstrText, const_cast<LPTSTR*>(&pstrText), 16);
                    aColorArray.Add((LPVOID)clrColor);
                    ::SetTextColor(hDC, RGB(GetBValue(clrColor), GetGValue(clrColor), GetRValue(clrColor)));
                }
                break;
            case _T('f'):  // Font
                {
                    pstrText++;
                    while( *pstrText > _T('\0') && *pstrText <= _T(' ') ) pstrText = ::CharNext(pstrText);
                    LPCTSTR pstrTemp = pstrText;
                    int iFont = (int) _tcstol(pstrText, const_cast<LPTSTR*>(&pstrText), 10);
                    //if( isdigit(*pstrText) ) { // debug版本会引起异常
                    if( pstrTemp != pstrText ) {
                        TFontInfo* pFontInfo = pManager->GetFontInfo(iFont);
                        aFontArray.Add(pFontInfo);
                        pTm = &pFontInfo->tm;
                        ::SelectObject(hDC, pFontInfo->hFont);
                    }
                    else {
                        CStdString sFontName;
                        int iFontSize = 10;
                        CStdString sFontAttr;
                        bool bBold = false;
                        bool bUnderline = false;
                        bool bItalic = false;
                        while( *pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}') && *pstrText != _T(' ') ) {
                            pstrTemp = ::CharNext(pstrText);
                            while( pstrText < pstrTemp) {
                                sFontName += *pstrText++;
                            }
                        }
                        while( *pstrText > _T('\0') && *pstrText <= _T(' ') ) pstrText = ::CharNext(pstrText);
                        if( isdigit(*pstrText) ) {
                            iFontSize = (int) _tcstol(pstrText, const_cast<LPTSTR*>(&pstrText), 10);
                        }
                        while( *pstrText > _T('\0') && *pstrText <= _T(' ') ) pstrText = ::CharNext(pstrText);
                        while( *pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}') ) {
                            pstrTemp = ::CharNext(pstrText);
                            while( pstrText < pstrTemp) {
                                sFontAttr += *pstrText++;
                            }
                        }
                        sFontAttr.MakeLower();
                        if( sFontAttr.Find(_T("bold")) >= 0 ) bBold = true;
                        if( sFontAttr.Find(_T("underline")) >= 0 ) bUnderline = true;
                        if( sFontAttr.Find(_T("italic")) >= 0 ) bItalic = true;
                        HFONT hFont = pManager->GetFont(sFontName, iFontSize, bBold, bUnderline, bItalic);
                        if( hFont == NULL ) hFont = pManager->AddFont(sFontName, iFontSize, bBold, bUnderline, bItalic);
                        TFontInfo* pFontInfo = pManager->GetFontInfo(hFont);
                        aFontArray.Add(pFontInfo);
                        pTm = &pFontInfo->tm;
                        ::SelectObject(hDC, pFontInfo->hFont);
                    }
                    cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
                }
                break;
            case _T('i'):  // Italic or Image
                {    
                    pstrNextStart = pstrText - 1;
                    pstrText++;
					CStdString sImageString = pstrText;
                    int iWidth = 0;
                    int iHeight = 0;
                    while( *pstrText > _T('\0') && *pstrText <= _T(' ') ) pstrText = ::CharNext(pstrText);
                    const TImageInfo* pImageInfo = NULL;
                    CStdString sName;
                    while( *pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}') && *pstrText != _T(' ') ) {
                        LPCTSTR pstrTemp = ::CharNext(pstrText);
                        while( pstrText < pstrTemp) {
                            sName += *pstrText++;
                        }
                    }
                    if( sName.IsEmpty() ) { // Italic
                        pstrNextStart = NULL;
                        TFontInfo* pFontInfo = pManager->GetDefaultFontInfo();
                        if( aFontArray.GetSize() > 0 ) pFontInfo = (TFontInfo*)aFontArray.GetAt(aFontArray.GetSize() - 1);
                        if( pFontInfo->bItalic == false ) {
                            HFONT hFont = pManager->GetFont(pFontInfo->sFontName, pFontInfo->iSize, pFontInfo->bBold, pFontInfo->bUnderline, true);
                            if( hFont == NULL ) hFont = pManager->AddFont(pFontInfo->sFontName, pFontInfo->iSize, pFontInfo->bBold, pFontInfo->bUnderline, true);
                            pFontInfo = pManager->GetFontInfo(hFont);
                            aFontArray.Add(pFontInfo);
                            pTm = &pFontInfo->tm;
                            ::SelectObject(hDC, pFontInfo->hFont);
                            cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
                        }
                    }
                    else {
                        while( *pstrText > _T('\0') && *pstrText <= _T(' ') ) pstrText = ::CharNext(pstrText);
                        int iImageListNum = (int) _tcstol(pstrText, const_cast<LPTSTR*>(&pstrText), 10);
						if( iImageListNum <= 0 ) iImageListNum = 1;
						while( *pstrText > _T('\0') && *pstrText <= _T(' ') ) pstrText = ::CharNext(pstrText);
						int iImageListIndex = (int) _tcstol(pstrText, const_cast<LPTSTR*>(&pstrText), 10);
						if( iImageListIndex < 0 || iImageListIndex >= iImageListNum ) iImageListIndex = 0;

						if( _tcsstr(sImageString.GetData(), _T("file=\'")) != NULL || _tcsstr(sImageString.GetData(), _T("res=\'")) != NULL ) {
							CStdString sImageResType;
							CStdString sImageName;
							LPCTSTR pStrImage = sImageString.GetData();
							CStdString sItem;
							CStdString sValue;
							while( *pStrImage != _T('\0') ) {
								sItem.Empty();
								sValue.Empty();
								while( *pStrImage > _T('\0') && *pStrImage <= _T(' ') ) pStrImage = ::CharNext(pStrImage);
								while( *pStrImage != _T('\0') && *pStrImage != _T('=') && *pStrImage > _T(' ') ) {
									LPTSTR pstrTemp = ::CharNext(pStrImage);
									while( pStrImage < pstrTemp) {
										sItem += *pStrImage++;
									}
								}
								while( *pStrImage > _T('\0') && *pStrImage <= _T(' ') ) pStrImage = ::CharNext(pStrImage);
								if( *pStrImage++ != _T('=') ) break;
								while( *pStrImage > _T('\0') && *pStrImage <= _T(' ') ) pStrImage = ::CharNext(pStrImage);
								if( *pStrImage++ != _T('\'') ) break;
								while( *pStrImage != _T('\0') && *pStrImage != _T('\'') ) {
									LPTSTR pstrTemp = ::CharNext(pStrImage);
									while( pStrImage < pstrTemp) {
										sValue += *pStrImage++;
									}
								}
								if( *pStrImage++ != _T('\'') ) break;
								if( !sValue.IsEmpty() ) {
									if( sItem == _T("file") || sItem == _T("res") ) {
										sImageName = sValue;
									}
									else if( sItem == _T("restype") ) {
										sImageResType = sValue;
									}
								}
								if( *pStrImage++ != _T(' ') ) break;
							}

							pImageInfo = pManager->GetImageEx((LPCTSTR)sImageName, sImageResType);
						}
						else
							pImageInfo = pManager->GetImageEx((LPCTSTR)sName);

						if( pImageInfo ) {
							iWidth = pImageInfo->nX;
							iHeight = pImageInfo->nY;
							if( iImageListNum > 1 ) iWidth /= iImageListNum;

                            if( pt.x + iWidth > rc.right && pt.x > rc.left && (uStyle & DT_SINGLELINE) == 0 ) {
                                bLineEnd = true;
                            }
                            else {
                                pstrNextStart = NULL;
                                if( bDraw && bLineDraw ) {
                                    CRect rcImage(pt.x, pt.y + cyLineHeight - iHeight, pt.x + iWidth, pt.y + cyLineHeight);
                                    if( iHeight < cyLineHeight ) { 
                                        rcImage.bottom -= (cyLineHeight - iHeight) / 2;
                                        rcImage.top = rcImage.bottom -  iHeight;
                                    }
                                    CRect rcBmpPart(0, 0, iWidth, iHeight);
                                    rcBmpPart.left = iWidth * iImageListIndex;
                                    rcBmpPart.right = iWidth * (iImageListIndex + 1);
                                    CRect rcCorner(0, 0, 0, 0);
									DrawImage(hDC, pImageInfo->hBitmap, rcImage,pRect?(*pRect):rcImage, rcBmpPart, rcCorner, \
                                        pImageInfo->alphaChannel, 255);
                                }

                                cyLine = MAX(iHeight, cyLine);
                                pt.x += iWidth;
                                cyMinHeight = pt.y + iHeight;
                                cxMaxWidth = MAX(cxMaxWidth, pt.x);
                            }
                        }
                        else pstrNextStart = NULL;
                    }
                }
                break;
            case _T('n'):  // Newline
                {
                    pstrText++;
                    if( (uStyle & DT_SINGLELINE) != 0 ) break;
                    bLineEnd = true;
                }
                break;
            case _T('p'):  // Paragraph
                {
                    pstrText++;
                    if( pt.x > rc.left ) bLineEnd = true;
                    while( *pstrText > _T('\0') && *pstrText <= _T(' ') ) pstrText = ::CharNext(pstrText);
                    int cyLineExtra = (int)_tcstol(pstrText, const_cast<LPTSTR*>(&pstrText), 10);
                    aPIndentArray.Add((LPVOID)cyLineExtra);
                    cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + cyLineExtra);
                }
                break;
            case _T('r'):  // Raw Text
                {
                    pstrText++;
                    bInRaw = true;
                }
                break;
            case _T('s'):  // Selected text background color
                {
                    pstrText++;
                    bInSelected = !bInSelected;
                    if( bDraw && bLineDraw ) {
                        if( bInSelected ) ::SetBkMode(hDC, OPAQUE);
                        else ::SetBkMode(hDC, TRANSPARENT);
                    }
                }
                break;
            case _T('u'):  // Underline text
                {
                    pstrText++;
                    TFontInfo* pFontInfo = pManager->GetDefaultFontInfo();
                    if( aFontArray.GetSize() > 0 ) pFontInfo = (TFontInfo*)aFontArray.GetAt(aFontArray.GetSize() - 1);
                    if( pFontInfo->bUnderline == false ) {
                        HFONT hFont = pManager->GetFont(pFontInfo->sFontName, pFontInfo->iSize, pFontInfo->bBold, true, pFontInfo->bItalic);
                        if( hFont == NULL ) hFont = pManager->AddFont(pFontInfo->sFontName, pFontInfo->iSize, pFontInfo->bBold, true, pFontInfo->bItalic);
                        pFontInfo = pManager->GetFontInfo(hFont);
                        aFontArray.Add(pFontInfo);
                        pTm = &pFontInfo->tm;
                        ::SelectObject(hDC, pFontInfo->hFont);
                        cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
                    }
                }
                break;
            case _T('x'):  // X Indent
                {
                    pstrText++;
                    while( *pstrText > _T('\0') && *pstrText <= _T(' ') ) pstrText = ::CharNext(pstrText);
                    int iWidth = (int) _tcstol(pstrText, const_cast<LPTSTR*>(&pstrText), 10);
                    pt.x += iWidth;
                    cxMaxWidth = MAX(cxMaxWidth, pt.x);
                }
                break;
            case _T('y'):  // Y Indent
                {
                    pstrText++;
                    while( *pstrText > _T('\0') && *pstrText <= _T(' ') ) pstrText = ::CharNext(pstrText);
                    cyLine = (int) _tcstol(pstrText, const_cast<LPTSTR*>(&pstrText), 10);
                }
                break;
                }
                if( pstrNextStart != NULL ) pstrText = pstrNextStart;
                else {
                    while( *pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}') ) pstrText = ::CharNext(pstrText);
                    pstrText = ::CharNext(pstrText);
                }
        }
        else if( !bInRaw && ( *pstrText == _T('<') || *pstrText == _T('{') ) && pstrText[1] == _T('/') )
        {
            pstrText++;
            pstrText++;
            switch( *pstrText )
            {
            case _T('c'):
                {
                    pstrText++;
                    aColorArray.Remove(aColorArray.GetSize() - 1);
                    DWORD clrColor = dwTextColor;
                    if( aColorArray.GetSize() > 0 ) clrColor = (int)aColorArray.GetAt(aColorArray.GetSize() - 1);
                    ::SetTextColor(hDC, RGB(GetBValue(clrColor), GetGValue(clrColor), GetRValue(clrColor)));
                }
                break;
            case _T('p'):
                pstrText++;
                if( pt.x > rc.left ) bLineEnd = true;
                aPIndentArray.Remove(aPIndentArray.GetSize() - 1);
                cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
                break;
            case _T('s'):
                {
                    pstrText++;
                    bInSelected = !bInSelected;
                    if( bDraw && bLineDraw ) {
                        if( bInSelected ) ::SetBkMode(hDC, OPAQUE);
                        else ::SetBkMode(hDC, TRANSPARENT);
                    }
                }
                break;
            case _T('a'):
                {
                    if( iLinkIndex < nLinkRects ) {
                        if( !bLineDraw ) ::SetRect(&prcLinks[iLinkIndex], ptLinkStart.x, ptLinkStart.y, MIN(pt.x, rc.right), pt.y + pTm->tmHeight + pTm->tmExternalLeading);
                        iLinkIndex++;
                    }
                    aColorArray.Remove(aColorArray.GetSize() - 1);
                    DWORD clrColor = dwTextColor;
                    if( aColorArray.GetSize() > 0 ) clrColor = (int)aColorArray.GetAt(aColorArray.GetSize() - 1);
                    ::SetTextColor(hDC, RGB(GetBValue(clrColor), GetGValue(clrColor), GetRValue(clrColor)));
                    bInLink = false;
                }
            case _T('b'):
            case _T('f'):
            case _T('i'):
            case _T('u'):
                {
                    pstrText++;
                    aFontArray.Remove(aFontArray.GetSize() - 1);
                    TFontInfo* pFontInfo = (TFontInfo*)aFontArray.GetAt(aFontArray.GetSize() - 1);
                    if( pFontInfo == NULL ) pFontInfo = pManager->GetDefaultFontInfo();
                    if( pTm->tmItalic && pFontInfo->bItalic == false ) {
                        ABC abc;
                        ::GetCharABCWidths(hDC, _T(' '), _T(' '), &abc);
                        pt.x += abc.abcC / 2; // 简单修正一下斜体混排的问题, 正确做法应该是http://support.microsoft.com/kb/244798/en-us
                    }
                    pTm = &pFontInfo->tm;
                    ::SelectObject(hDC, pFontInfo->hFont);
                    cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
                }
                break;
            }
            while( *pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}') ) pstrText = ::CharNext(pstrText);
            pstrText = ::CharNext(pstrText);
        }
        else if( !bInRaw &&  *pstrText == _T('<') && pstrText[2] == _T('>') && (pstrText[1] == _T('{')  || pstrText[1] == _T('}')) )
        {
            SIZE szSpace = { 0 };
            ::GetTextExtentPoint32(hDC, &pstrText[1], 1, &szSpace);
#ifndef _WIN32_WCE
            if( bDraw && bLineDraw ) ::TextOut(hDC, pt.x, pt.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading, &pstrText[1], 1);
#else
			if(bDraw && bLineDraw)
				::ExtTextOut(hDC,pt.x, pt.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading,ETO_CLIPPED,pRect, &pstrText[1], 1,NULL);
#endif
            pt.x += szSpace.cx;
            cxMaxWidth = MAX(cxMaxWidth, pt.x);
            pstrText++;pstrText++;pstrText++;
        }
        else if( !bInRaw &&  *pstrText == _T('{') && pstrText[2] == _T('}') && (pstrText[1] == _T('<')  || pstrText[1] == _T('>')) )
        {
            SIZE szSpace = { 0 };
            ::GetTextExtentPoint32(hDC, &pstrText[1], 1, &szSpace);
#ifndef _WIN32_WCE
            if( bDraw && bLineDraw ) ::TextOut(hDC, pt.x,  pt.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading, &pstrText[1], 1);
#else
			if(bDraw && bLineDraw) 
				::ExtTextOut(hDC, pt.x,  pt.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading,ETO_CLIPPED,pRect, &pstrText[1], 1,NULL);
#endif
            pt.x += szSpace.cx;
            cxMaxWidth = MAX(cxMaxWidth, pt.x);
            pstrText++;pstrText++;pstrText++;
        }
        else if( !bInRaw &&  *pstrText == _T(' ') )
        {
            SIZE szSpace = { 0 };
            ::GetTextExtentPoint32(hDC, _T(" "), 1, &szSpace);
            // Still need to paint the space because the font might have
            // underline formatting.
#ifndef _WIN32_WCE
            if( bDraw && bLineDraw ) ::TextOut(hDC, pt.x,  pt.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading, _T(" "), 1);
#else
			if( bDraw && bLineDraw ) 
				::ExtTextOut(hDC, pt.x,  pt.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading,ETO_CLIPPED,pRect, _T(" "), 1,NULL);
#endif
            pt.x += szSpace.cx;
            cxMaxWidth = MAX(cxMaxWidth, pt.x);
            pstrText++;
        }
        else
        {
            POINT ptPos = pt;
            int cchChars = 0;
            int cchSize = 0;
            int cchLastGoodWord = 0;
            int cchLastGoodSize = 0;
            LPCTSTR p = pstrText;
            LPCTSTR pstrNext;
            SIZE szText = { 0 };
            if( !bInRaw && *p == _T('<') || *p == _T('{') ) p++, cchChars++, cchSize++;
            while( *p != _T('\0') && *p != _T('\n') ) {
                // This part makes sure that we're word-wrapping if needed or providing support
                // for DT_END_ELLIPSIS. Unfortunately the GetTextExtentPoint32() call is pretty
                // slow when repeated so often.
                // TODO: Rewrite and use GetTextExtentExPoint() instead!
                if( bInRaw ) {
                    if( ( *p == _T('<') || *p == _T('{') ) && p[1] == _T('/') 
                        && p[2] == _T('r') && ( p[3] == _T('>') || p[3] == _T('}') ) ) {
                            p += 4;
                            bInRaw = false;
                            break;
                    }
                }
                else {
                    if( *p == _T('<') || *p == _T('{') ) break;
                }
                pstrNext = ::CharNext(p);
                cchChars++;
                cchSize += (int)(pstrNext - p);
                szText.cx = cchChars * pTm->tmMaxCharWidth;
                if( pt.x + szText.cx >= rc.right ) {
                    ::GetTextExtentPoint32(hDC, pstrText, cchSize, &szText);
                }
                if( pt.x + szText.cx > rc.right ) {
                    if( pt.x + szText.cx > rc.right && pt.x != rc.left) {
                        cchChars--;
                        cchSize -= (int)(pstrNext - p);
                    }
                    if( (uStyle & DT_WORDBREAK) != 0 && cchLastGoodWord > 0 ) {
                        cchChars = cchLastGoodWord;
                        cchSize = cchLastGoodSize;                 
                    }
                    if( (uStyle & DT_END_ELLIPSIS) != 0 && cchChars > 0 ) {
                        cchChars -= 1;
                        LPCTSTR pstrPrev = ::CharPrev(pstrText, p);
                        if( cchChars > 0 ) {
                            cchChars -= 1;
                            pstrPrev = ::CharPrev(pstrText, pstrPrev);
                            cchSize -= (int)(p - pstrPrev);
                        }
                        else 
                            cchSize -= (int)(p - pstrPrev);
                        pt.x = rc.right;
                    }
                    bLineEnd = true;
                    cxMaxWidth = MAX(cxMaxWidth, pt.x);
                    break;
                }
                if (!( ( p[0] >= _T('a') && p[0] <= _T('z') ) || ( p[0] >= _T('A') && p[0] <= _T('Z') ) )) {
                    cchLastGoodWord = cchChars;
                    cchLastGoodSize = cchSize;
                }
                if( *p == _T(' ') ) {
                    cchLastGoodWord = cchChars;
                    cchLastGoodSize = cchSize;
                }
                p = ::CharNext(p);
            }
            
            ::GetTextExtentPoint32(hDC, pstrText, cchSize, &szText);
            if( bDraw && bLineDraw ) {
#ifndef _WIN32_WCE
                ::TextOut(hDC, ptPos.x, ptPos.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading, pstrText, cchSize);
#else
				::ExtTextOut(hDC, ptPos.x, ptPos.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading,ETO_CLIPPED,pRect, pstrText, cchSize,NULL);
#endif
                if( pt.x >= rc.right && (uStyle & DT_END_ELLIPSIS) != 0 ) 
#ifndef _WIN32_WCE
                    ::TextOut(hDC, ptPos.x + szText.cx, ptPos.y, _T("..."), 3);
#else
					::ExtTextOut(hDC, ptPos.x + szText.cx, ptPos.y,ETO_CLIPPED,pRect, _T("..."), 3,NULL);
#endif
            }
            pt.x += szText.cx;
            cxMaxWidth = MAX(cxMaxWidth, pt.x);
            pstrText += cchSize;
        }

        if( pt.x >= rc.right || *pstrText == _T('\n') || *pstrText == _T('\0') ) bLineEnd = true;
        if( bDraw && bLineEnd ) {
            if( !bLineDraw ) {
                aFontArray.Resize(aLineFontArray.GetSize());
                ::CopyMemory(aFontArray.GetData(), aLineFontArray.GetData(), aLineFontArray.GetSize() * sizeof(LPVOID));
                aColorArray.Resize(aLineColorArray.GetSize());
                ::CopyMemory(aColorArray.GetData(), aLineColorArray.GetData(), aLineColorArray.GetSize() * sizeof(LPVOID));
                aPIndentArray.Resize(aLinePIndentArray.GetSize());
                ::CopyMemory(aPIndentArray.GetData(), aLinePIndentArray.GetData(), aLinePIndentArray.GetSize() * sizeof(LPVOID));

                cyLineHeight = cyLine;
                pstrText = pstrLineBegin;
                bInRaw = bLineInRaw;
                bInSelected = bLineInSelected;

                DWORD clrColor = dwTextColor;
                if( aColorArray.GetSize() > 0 ) clrColor = (int)aColorArray.GetAt(aColorArray.GetSize() - 1);
                ::SetTextColor(hDC, RGB(GetBValue(clrColor), GetGValue(clrColor), GetRValue(clrColor)));
                TFontInfo* pFontInfo = (TFontInfo*)aFontArray.GetAt(aFontArray.GetSize() - 1);
                if( pFontInfo == NULL ) pFontInfo = pManager->GetDefaultFontInfo();
                pTm = &pFontInfo->tm;
                ::SelectObject(hDC, pFontInfo->hFont);
                if( bInSelected ) ::SetBkMode(hDC, OPAQUE);
            }
            else {
                aLineFontArray.Resize(aFontArray.GetSize());
                ::CopyMemory(aLineFontArray.GetData(), aFontArray.GetData(), aFontArray.GetSize() * sizeof(LPVOID));
                aLineColorArray.Resize(aColorArray.GetSize());
                ::CopyMemory(aLineColorArray.GetData(), aColorArray.GetData(), aColorArray.GetSize() * sizeof(LPVOID));
                aLinePIndentArray.Resize(aPIndentArray.GetSize());
                ::CopyMemory(aLinePIndentArray.GetData(), aPIndentArray.GetData(), aPIndentArray.GetSize() * sizeof(LPVOID));
                pstrLineBegin = pstrText;
                bLineInSelected = bInSelected;
                bLineInRaw = bInRaw;
            }
        }

        ASSERT(iLinkIndex<=nLinkRects);
    }

    nLinkRects = iLinkIndex;

    // Return size of text when requested
    if( (uStyle & DT_CALCRECT) != 0 ) {
        rc.bottom = MAX(cyMinHeight, pt.y + cyLine);
        rc.right = MIN(rc.right, cxMaxWidth);
    }

    if( bDraw ) ::SelectClipRgn(hDC, hOldRgn);
    ::DeleteObject(hOldRgn);
    ::DeleteObject(hRgn);

    ::SelectObject(hDC, hOldFont);
}

HBITMAP CRenderEngine::GenerateBitmap(CPaintManagerUI* pManager, CControlUI* pControl, RECT rc)
{
    int cx = rc.right - rc.left;
    int cy = rc.bottom - rc.top;

    HDC hPaintDC = ::CreateCompatibleDC(pManager->GetPaintDC());
    HBITMAP hPaintBitmap = ::CreateCompatibleBitmap(pManager->GetPaintDC(), rc.right, rc.bottom);
    ASSERT(hPaintDC);
    ASSERT(hPaintBitmap);
    HBITMAP hOldPaintBitmap = (HBITMAP) ::SelectObject(hPaintDC, hPaintBitmap);
    pControl->DoPaint(hPaintDC, rc);

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = cx * cy * sizeof(DWORD);
    LPDWORD pDest = NULL;
    HDC hCloneDC = ::CreateCompatibleDC(pManager->GetPaintDC());
    HBITMAP hBitmap = ::CreateDIBSection(pManager->GetPaintDC(), &bmi, DIB_RGB_COLORS, (LPVOID*) &pDest, NULL, 0);
    ASSERT(hCloneDC);
    ASSERT(hBitmap);
    if( hBitmap != NULL )
    {
        HBITMAP hOldBitmap = (HBITMAP) ::SelectObject(hCloneDC, hBitmap);
        ::BitBlt(hCloneDC, 0, 0, cx, cy, hPaintDC, rc.left, rc.top, SRCCOPY);
        ::SelectObject(hCloneDC, hOldBitmap);
        ::DeleteDC(hCloneDC);  
#ifndef _WIN32_WCE
        ::GdiFlush();
#endif
    }

    // Cleanup
    ::SelectObject(hPaintDC, hOldPaintBitmap);
    ::DeleteObject(hPaintBitmap);
    ::DeleteDC(hPaintDC);

    return hBitmap;
}

} // namespace DuiLib
