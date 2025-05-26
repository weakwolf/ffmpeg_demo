
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

// 将全局变量或者函数声明为static是为了让它仅在本文件中可见
static AVFormatContext* pFmtCtx = NULL;
static AVCodecContext* pVideoDecCtx = NULL;
static AVCodecContext* pAudioDecCtx = NULL;
const char* pFilePath = NULL;

/**
 * 打开一个流
 *
 * @param pStreamIdx	流索引
 * @param ppDecCtx		流解码器上下文
 * @param pFmtCtx		封装格式上下文
 * @param type			期望的流类型
 * @return				>=0成功，<0失败
 */
static int OpenCodecContext(OUT int* pStreamIdx, OUT AVCodecContext** ppDecCtx, IN AVFormatContext* pFmtCtx, IN AVMediaType type)
{
	int iRet = -1;;
	int iStreamIndex = -1;
	AVStream* pStream = NULL;
	const AVCodec* pDec = NULL;

	iRet = av_find_best_stream(pFmtCtx, type, -1, -1, NULL, 0);
	if (iRet < 0)
	{
		fprintf(stderr, "Could not find %s stream in input file '%s'\n",
			av_get_media_type_string(type),pFilePath);

		return iRet;
	}
	iStreamIndex = iRet;
	pStream = pFmtCtx->streams[iStreamIndex];

	// 尝试获取解码器
	pDec = avcodec_find_decoder(pStream->codecpar->codec_id);
	if (!pDec)
	{
		fprintf(stderr, "Failed to find %s codec\n",
			av_get_media_type_string(type));

		// 返回无效参数错误码
		return AVERROR(EINVAL);
	}
	// 为解码器分配一个解码器上下文
	*ppDecCtx = avcodec_alloc_context3(pDec);
	if (!*ppDecCtx)
	{
		fprintf(stderr, "Failed to allocate the %s codec context\n",
			av_get_media_type_string(type));
		return AVERROR(ENOMEM);
	}
	// 将流的编解码参数复制给解码器上下文
	if (iRet = avcodec_parameters_to_context(*ppDecCtx, pStream->codecpar))
	{
		fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
			av_get_media_type_string(type));
		return iRet;
	}
	// 初始化解码器
	if (iRet = avcodec_open2(*ppDecCtx, pDec, NULL))
	{
		fprintf(stderr, "Failed to open %s codec\n",
			av_get_media_type_string(type));
		return iRet;
	}

	*pStreamIdx = iStreamIndex;
}

int main(int argc,char* argv[])
{
	if (argc < 2)
	{
		Log("please choose a file!!!");

		return -1;
	}

	pFilePath = argv[1];
	// 我们让接口自己分配pFmtCtx的内存
	int iRes = avformat_open_input(&pFmtCtx, pInput, NULL, NULL);
	if (iRes < 0)
	{
		PRINT_FFMPEG_ERROR("failed to open input",iRes);

		goto end;
	}
	iRes = avformat_find_stream_info(pFmtCtx, NULL);
	if (iRes < 0)
	{
		PRINT_FFMPEG_ERROR("failed to find stream info",iRes);

		goto end;
	}

	// 处理视频流
	// 判断是否存在视频流
#if 1
	int iVideoStream = -1;
	iVideoStream = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
#else
	for (int i = 0; i < pFmtCtx->nb_streams; ++i)
	{
		if (AVMEDIA_TYPE_VIDEO == pFmtCtx->streams[i]->codecpar->codec_type)
		{
			iVideoStream = i;
			break;
		}
	}
#endif
	if (iVideoStream < 0)
	{
		Log("there is no video stream!!!\n");
		avformat_close_input(&pFmtCtx);

		return -1;
	}
	const AVCodec* pDecoder = avcodec_find_decoder(pFmtCtx->streams[iVideoStream]->codecpar->codec_id);
	if (!pDecoder)
	{
		Log("there is no valid decoder!!!\n");
		avformat_close_input(&pFmtCtx);
		avformat_free_context(pFmtCtx);

		return -1;
	}
	AVCodecContext* pCodecCtx = avcodec_alloc_context3(pDecoder);
	avcodec_parameters_to_context(pCodecCtx, pFmtCtx->streams[iVideoStream]->codecpar);
	iRes = avcodec_open2(pCodecCtx, pDecoder, NULL);
	if (iRes < 0)
	{
		PRINT_FFMPEG_ERROR("open decoder error:", iRes);
		avcodec_free_context(&pCodecCtx);
		avformat_close_input(&pFmtCtx);
		avformat_free_context(pFmtCtx);

		return -1;
	}


end:
	avcodec_free_context(&pCodecCtx);
	avformat_close_input(&pFmtCtx);

	return 0;
}

