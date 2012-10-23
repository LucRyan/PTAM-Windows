// This VideoSource for Win32 uses EWCLIB
//
// EWCLIB ver.1.2
// http://www.geocities.jp/in_subaru/ewclib/index.html

#define WIN32_LEAN_AND_MEAN
#include "VideoSource.h"
#include <Windows.h>
#include <cvd/utility.h>
#include "ewclib.h"

using namespace CVD;
using namespace std;

#define CAPTURE_SIZE_X	640
#define CAPTURE_SIZE_Y	480
#define FPS				30

VideoSource::VideoSource()
{
	EWC_Open(MEDIASUBTYPE_RGB24, CAPTURE_SIZE_X, CAPTURE_SIZE_Y, FPS);
    m_buffer = new unsigned char[EWC_GetBufferSize(0)];

	mirSize.x = CAPTURE_SIZE_X;
	mirSize.y = CAPTURE_SIZE_Y;
};

VideoSource::~VideoSource()
{
    EWC_Close();
    delete[] m_buffer;
}

void VideoSource::GetAndFillFrameBWandRGB(Image<CVD::byte> &imBW, Image<CVD::Rgb<CVD::byte> > &imRGB)
{
	EWC_GetImage(0, m_buffer);

	unsigned char* pImage = m_buffer;

	BasicImage<CVD::byte> imCaptured(pImage, mirSize);
	imRGB.resize(mirSize);
	imBW.resize(mirSize);

	for (int y=0; y<mirSize.y; y++) {
		for (int x=0; x<mirSize.x; x++) {
			imRGB[y][x].blue = *pImage;
			pImage++;

			imRGB[y][x].green = *pImage;
			imBW[y][x]        = *pImage;
			pImage++;

			imRGB[y][x].red = *pImage;
			pImage++;
		}
	}

}

ImageRef VideoSource::Size()
{
	return mirSize;
}