
/**
 * 一个简单的视频播放器，base to https://blog.csdn.net/leixiaohua1020/article/details/38868499
 * 致敬雷神！！！
 * 坚持study!!!
 */

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/avutil.h"
#include "libavutil/error.h"
#include "SDL2/SDL.h"
}

#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"swscale.lib")
#pragma comment(lib,"avutil.lib")

#ifdef _DEBUG
#pragma comment(lib,"SDL2d.lib")
#pragma comment(lib,"manual-link/SDL2maind.lib")
#else
#pragma comment(lib,"SDL2.lib")
#pragma comment(lib,"manual-link/SDL2main.lib")
#endif

#include <cstdio>

#include "utils.h"

int main(int argc,char* argv[])
{
	if (argc < 2)
	{
		Log("please choose a file!!!");

		return -1;
	}

	const char* pInput = argv[1];
	AVFormatContext* pFormat = NULL;
	pFormat = avformat_alloc_context();
	if (NULL == pFormat)
	{
		Log("alloc error");

		return -1;
	}
	int iRes = avformat_open_input(&pFormat, pInput, NULL, NULL);
	if (iRes < 0)
	{
		Log("failed to open input");

		return -1;
	}
	iRes = avformat_find_stream_info(pFormat, NULL);
	if (iRes < 0)
	{
		Log("failed to find stream info");

		return -1;
	}

	return 0;
}

