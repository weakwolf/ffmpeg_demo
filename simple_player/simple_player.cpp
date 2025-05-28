
/**
 * 一个简单的视频播放器，base to https://blog.csdn.net/leixiaohua1020/article/details/38868499 and ffmpeg official demo
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
#include <libavutil/imgutils.h>
#include "SDL2/SDL.h"
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
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
#include <cstdint>
#include "utils.h"		

// 将全局变量或者函数声明为static是为了让它仅在本文件中可见

static AVFormatContext* pFmtCtx		= NULL;
static AVCodecContext* pVideoDecCtx = NULL;
static AVCodecContext* pAudioDecCtx = NULL;

static const char*	pFilePath		= NULL;
static const char*	pVideoOutput	= NULL;	// 视频帧输出地址
static const char*	pAudioOutput	= NULL;	// 音频帧输出地址
static FILE*		pVideoFile		= NULL;	// 视频文件描述符
static FILE*		pAudioFile		= NULL;	// 音频文件描述符

static int			iVideoIndex		= -1;		// 视频流索引
static int			iAudioIndex		= -1;		// 音频流索引
static AVStream*	pVideoStream	= NULL;
static AVStream*	pAudioStream	= NULL;

static int				iWidth			= -1;				// 视频宽
static int				iHeight			= -1;				// 视频高
static AVPixelFormat	pixFmt			= AV_PIX_FMT_NONE;	// 视频颜色空间
static uint8_t*			videoData[4]	= { NULL };			// 存储视频的各plane
static int				videoLinesize[4];
static int				iVideoBufSize	= -1;

static AVPacket*	pPkt				= NULL;
static AVFrame*		pFrame				= NULL;
static int			iVideoFrameCount	= 0;
static int			iAudioFrameCount	= 0;


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

	return 0;
}

static int OutputVideoFrame(AVFrame* pFrame)
{
	if (pFrame->width != iWidth || pFrame->height != iHeight || pFrame->format != pixFmt)
	{
		fprintf(stderr, "Error: Width, height and pixel format have to be "
			"constant in a rawvideo file, but the width, height or "
			"pixel format of the input video changed:\n"
			"old: width = %d, height = %d, format = %s\n"
			"new: width = %d, height = %d, format = %s\n",
			iWidth, iHeight, av_get_pix_fmt_name(pixFmt),
			pFrame->width, pFrame->height,
			av_get_pix_fmt_name((AVPixelFormat)pFrame->format));

		return -1;
	}

	printf("video frame index:%d\n", iVideoFrameCount++);

	av_image_copy2(videoData, videoLinesize, pFrame->data,
		pFrame->linesize, pixFmt,
		iWidth, iHeight);

	fwrite(videoData[0], 1, iVideoBufSize, pVideoFile);

	return 0;
}

static int OutputAudioFrame(AVFrame* pFrame)
{
	size_t iUnpaddedLinesize = pFrame->nb_samples * av_get_bytes_per_sample((AVSampleFormat)pFrame->format);

	printf("audio_frame n:%d nb_samples:%d pts:%lld time_base:%f \n",
		iAudioFrameCount++, pFrame->nb_samples,
		pFrame->pts, av_q2d(pAudioDecCtx->time_base));

	av_image_copy2(videoData, videoLinesize, pFrame->data,
		pFrame->linesize, pixFmt,
		iWidth, iHeight);

	fwrite(pFrame->extended_data[0], 1, iUnpaddedLinesize, pAudioFile);

	return 0;
}

static int DecodePacket(AVCodecContext* pDec, const AVPacket* pPkt)
{
	int iRet = 0;

	// 如果第二个参数为NULL，则代表清空所有缓冲
	iRet = avcodec_send_packet(pDec, pPkt);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("invalid AVPacket,error:\n", iRet);

		return iRet;
	}

	while (iRet >= 0)
	{
		iRet = avcodec_receive_frame(pDec, pFrame);
		if (iRet < 0)
		{
			// 返回AVERROR(EAGIN)代表需要更多packet才能解码，返回EOF代表没有更多的输出帧了，其他都是错误情况
			if (AVERROR_EOF == iRet || AVERROR(EAGAIN) == iRet)
				return 0;

			PRINT_FFMPEG_ERROR("Error during decoding:", iRet);
			return iRet;
		}

		if (AVMEDIA_TYPE_VIDEO == pVideoDecCtx->codec->type)
			iRet = OutputVideoFrame(pFrame);
		else
			iRet = OutputAudioFrame(pFrame);

		av_frame_unref(pFrame);
	}

	return iRet;
}

int main(int argc,char* argv[])
{
	if (argc != 4)
	{
		Log("invalid parameters",__FILE__,__LINE__);

		return -1;
	}
	pFilePath = argv[1];
	const char* pVideoOutput = argv[2];
	const char* pAudioOutput = argv[3];

	int iRet = avformat_open_input(&pFmtCtx, pFilePath, NULL, NULL);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("open input error:", iRet);

		return -1;
	}
	// 探查流信息
	iRet = avformat_find_stream_info(pFmtCtx, NULL);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("could not detect stream info:", iRet);

		goto end;
	}

	// 初始化视频流相关
	iRet = OpenCodecContext(&iVideoIndex, &pVideoDecCtx, pFmtCtx, AVMEDIA_TYPE_VIDEO);
	if (iRet >= 0)
	{
		pVideoStream = pFmtCtx->streams[iRet];
		pVideoFile = fopen(pVideoOutput, "wb+");
		if (!pVideoFile)
		{
			fprintf(stderr, "could not open video file\n");

			goto end;
		}

		// 分配内存空间用于存放解码数据
		iWidth = pVideoDecCtx->width;
		iHeight = pVideoDecCtx->height;
		pixFmt = pVideoDecCtx->pix_fmt;
		iRet = av_image_alloc(videoData, videoLinesize, iWidth, iHeight, pixFmt, 1);
		if (iRet < 0)
		{
			fprintf(stderr, "could not allocate image space\n");

			goto end;
		}
		iVideoBufSize = iRet;
	}
	else
	{
		fprintf(stderr, "could open video stream\n");

		goto end;
	}

	// 初始化音频流相关
	iRet = OpenCodecContext(&iAudioIndex, &pAudioDecCtx, pFmtCtx, AVMEDIA_TYPE_AUDIO);
	if (iRet >= 0)
	{
		pAudioStream = pFmtCtx->streams[iRet];
		pAudioFile = fopen(pAudioOutput, "wb+");
		if (!pAudioFile)
		{
			fprintf(stderr, "could not open audio file\n");

			goto end;
		}
	}
	else
	{
		fprintf(stderr, "could open audio stream\n");

		//goto end;
	}
	// 打印视音频元数据
	av_dump_format(pFmtCtx, 0, pFilePath, 0);
	if (!pVideoStream && !pAudioStream)
	{
		fprintf(stderr, "could not find video and audio stream\n");

		goto end;
	}

	if (pVideoStream)
		printf("Demuxing video from file '%s' into '%s'\n", pFilePath, pVideoOutput);
	if (pAudioStream)
		printf("Demuxing audio from file '%s' into '%s'\n", pFilePath, pAudioOutput);

	pFrame = av_frame_alloc();
	if (!pFrame)
	{
		fprintf(stderr, "could not allocate frame\n");

		goto end;
	}
	pPkt = av_packet_alloc();
	if (!pPkt)
	{
		fprintf(stderr, "could not allocate packet\n");

		goto end;
	}
	// 开始解码
	while (av_read_frame(pFmtCtx, pPkt) >= 0)
	{
		if (iVideoIndex == pPkt->stream_index)// 视频帧
			iRet = DecodePacket(pVideoDecCtx, pPkt);
		else if (iAudioIndex == pPkt->stream_index)// 音频帧
			iRet = DecodePacket(pAudioDecCtx, pPkt);
		else
			// TODO: other
		av_packet_unref(pPkt);

		if(iRet < 0)// 返回值小于0说明有未知错误发生
			break;
	}
	// flush decoders
	if (pVideoDecCtx)
		DecodePacket(pVideoDecCtx, NULL);
	if (pAudioDecCtx)
		DecodePacket(pAudioDecCtx, NULL);

end:// 资源释放
	av_packet_free(&pPkt);
	av_frame_free(&pFrame);
	// av_freep和av_free的区别在于，前者会将指针置为NULL
	av_freep(&videoData[0]);
	SAFE_POINTER_CALL(pAudioFile, fclose(pAudioFile));
	SAFE_POINTER_CALL(pVideoFile, fclose(pVideoFile));
	avcodec_free_context(&pVideoDecCtx);
	avcodec_free_context(&pAudioDecCtx);
	avformat_close_input(&pFmtCtx);

	return 0;
}

