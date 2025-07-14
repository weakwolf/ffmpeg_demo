extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/avutil.h"
#include "libavutil/error.h"
#include <libavutil/imgutils.h>
#include "SDL2/SDL.h"
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
}
#include "SDL2/SDL.h"

#include <mutex>
#include <condition_variable>

#include <cstdio>
#include <cstring>

#include <windows.h>

#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"swscale.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"swresample.lib")

#ifdef _DEBUG
#pragma comment(lib,"SDL2d.lib")
#pragma comment(lib,"manual-link/SDL2maind.lib")
#else
#pragma comment(lib,"SDL2.lib")
#pragma comment(lib,"manual-link/SDL2main.lib")
#endif	

#define PRINT_FFMPEG_ERROR(info,ret) char buf[128] = {0};\
							av_strerror(ret,buf,128);\
							fprintf(stderr,"%s:%s\n",info,buf);

//#define OUTPUT_BEFORE	// 是否将解码后，还没有重采样pcm数据保存到本地
//#define OUTPUT_AFTER	// 是否将解码后，重采样pcm数据保存到本地

#define MAX_BUFFER_SIZE_MP3	2 * 2 * 48000	// 1s的音频数据大小

static const char* pInput = NULL;
static AVFormatContext* pFmtCtx = NULL;
static AVCodecContext* pCodecCtx = NULL;
static const AVCodec* pCodec = NULL;
static int iAudioIndex = -1;	// 音频流索引
static AVPacket* pPkt = NULL;
static AVFrame* pFrame = NULL;

static const char* pTemp = NULL;

static SDL_AudioSpec wanted;	// 期望的音频格式
static SDL_AudioSpec real;		// 实际的音频格式
static SDL_AudioDeviceID dev;

static int iFrameSize = -1;
static unsigned char* pBuf = NULL;	// 存储重采样后的音频帧数据
static HANDLE hEvent1 = NULL;	// 用于解码线程通知sdl播放线程有数据了
static HANDLE hEvent2 = NULL;	// 用于sdl播放线程通知解码线程播放完成可以开始解码下一帧了
static bool bStart = false;	// SDL是否已经开始播放

static int iChannelCount = -1;
static int iFrequency = -1;
static AVSampleFormat inSampleFmt = AV_SAMPLE_FMT_NONE;	// 输入音频采样格式
static AVSampleFormat outSampleFmt = AV_SAMPLE_FMT_S16; // SDL期望的音频采样格式
static SwrContext* pConvertCtx = NULL;
static AVChannelLayout outChannelLayout;	// 输出声道布局，新版本的ffmpeg要求用这个结构体，不再使用整数类型，怎么感觉越高越复杂了
static AVChannelLayout inChannelLayout;		// 输入声道布局

static FILE* pOutAfter = NULL;
static FILE* pOutBefore = NULL;
static char pOutPath[260] = { 0 };


static void FillAudio(void* opaque, Uint8* pStream, int iLen)
{
	// 你可能想要在回调函数里面保证线程安全，调用SDL_LockAudioDevice
	// 这是错误的，该回调函数在调用之前默认会调用该函数来保证线程安全

	if (0 == iLen)
		return;

	WaitForSingleObject(hEvent1, 1000);
	printf("sdl play one frame\n");
	// SDL2必须先调用SDL_memset清空缓冲区，你可以试试不调会怎样
	SDL_memset(pStream, 0, iLen);
	iLen = iLen > iFrameSize ? iFrameSize : iLen;
	// 以下两种方式都可以
#if 0
	memcpy(pStream, pBuf, iLen);
#else
	SDL_MixAudioFormat(pStream, pBuf, AUDIO_S16LSB, iLen, SDL_MIX_MAXVOLUME);
#endif

	SetEvent(hEvent2);
}

static int DecodePacket(AVCodecContext* pCodecCtx, const AVPacket* pPkt)
{
	int iRet = avcodec_send_packet(pCodecCtx, pPkt);
	if (iRet < 0)
	{
		fprintf(stderr, "invalid packet\n");

		return iRet;
	}
	while (iRet >= 0)
	{
		iRet = avcodec_receive_frame(pCodecCtx, pFrame);
		if(iRet < 0)
		{
			// 没有更多输出帧或者需要更多数据才能解码
			if (AVERROR_EOF == iRet || AVERROR(EAGAIN) == iRet)
				return 0;

			PRINT_FFMPEG_ERROR("avcodec_receive_frame error", iRet);

			return iRet;
		}
#ifdef OUTPUT_BEFORE
		if (!pOutBefore)
		{
			sprintf(pOutPath, "../output/audio_%d_%d_%s_before.pcm", iChannelCount, iFrequency, "fltp");
			pOutBefore = fopen(pOutPath, "wb+");
		}
		if (pOutBefore)
		{
			// 按照官方的说法，对于立体声，最好使用extended_data来访问数据，听官方的准没错
			// 此时的linesize[0]是一个通道的音频数据的大小，其余通道的大小也是这个，这个数组的其他值都是0
			// 我很疑惑，为什么把两个通道一起写出去播放就不对，写单个通道就是正常的
			fwrite(pFrame->extended_data[0], 1, pFrame->linesize[0], pOutBefore);
			//fwrite(pFrame->extended_data[1], 1, pFrame->linesize[0], pOutBefore);
			//printf("%p",pFrame->extended_data[1]);
		}
#endif

		swr_convert(pConvertCtx, &pBuf, pFrame->nb_samples, (const uint8_t**)pFrame->data, pFrame->nb_samples);
		if (!bStart)
		{
			// 确保ffmpeg成功解码之后再让sdl开始播放
			SDL_PauseAudioDevice(dev, 0);
			bStart = true;
		}

		printf("notify sdl\n");
		// 通知sdl回调有音频数据了
		SetEvent(hEvent1);
		// 等待sdl回调处理完数据
		if (WAIT_TIMEOUT == WaitForSingleObject(hEvent2, 1000))
		{
			fprintf(stderr, "sdl call back function is wrong\n");

			return -1;
		}
		ResetEvent(hEvent2);

#ifdef OUTPUT_AFTER
		if (!pOutAfter)
		{
			sprintf(pOutPath, "../output/aduio_%d_%d_%s_after.pcm", iChannelCount, iFrequency, "s16");
			pOutAfter = fopen(pOutPath, "wb+");
		}
		if (pOutAfter)
		{
			fwrite(pBuf, 1, iFrameSize, pOutAfter);
		}
#endif
		av_frame_unref(pFrame);
	}

	return -1;
}

int main(int argc, char* argv[])
{
	if (2 != argc)
	{
		fprintf(stderr, "invalid parameters!!!\n");

		return -1;
	}

	pInput = argv[1];
	if (NULL == pInput)
	{
		fprintf(stderr, "no file path!!!\n");

		return -1;
	}

	// 初始化
	int iRet = avformat_open_input(&pFmtCtx, pInput, NULL, NULL);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("avformat_open_input failed", iRet);

		goto end;
	}
	// 探查流信息
	iRet = avformat_find_stream_info(pFmtCtx, NULL);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("could not detect stream info:", iRet);

		goto end;
	}
	iRet = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("av_find_best_stream failed", iRet);

		goto end;
	}
	iAudioIndex = iRet;
	// 初始化解码器
	pCodec = avcodec_find_decoder(pFmtCtx->streams[iAudioIndex]->codecpar->codec_id);
	if (NULL == pCodec)
	{
		fprintf(stderr, "could not find valid decoder\n");

		goto end;
	}
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (pCodecCtx < 0)
	{
		fprintf(stderr, "avcodec_alloc_context3 failed\n");

		goto end;
	}
	iRet = avcodec_parameters_to_context(pCodecCtx, pFmtCtx->streams[iAudioIndex]->codecpar);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("avcodec_parameters_to_context failed", iRet);

		goto end;
	}

	// 重采样相关
	iChannelCount = pCodecCtx->ch_layout.nb_channels;
	iFrequency = pCodecCtx->sample_rate;
	inSampleFmt = pCodecCtx->sample_fmt;
	iFrameSize = av_samples_get_buffer_size(NULL, iChannelCount, pCodecCtx->frame_size, outSampleFmt, 1);
	
	// 解码出来是AV_SAMPLE_FMT_FLTP类型，要转为AV_SAMPLE_FMT_S16后SDL才能播放
	// 创建解码上下文
	av_channel_layout_default(&inChannelLayout, iChannelCount);
	av_channel_layout_default(&outChannelLayout, 2);
	swr_alloc_set_opts2(&pConvertCtx, &outChannelLayout, AV_SAMPLE_FMT_S16, iFrequency,
		&inChannelLayout, inSampleFmt, iFrequency, 0, NULL);
	if (NULL == pConvertCtx)
	{
		fprintf(stderr, "Could not create resample convert context\n");

		goto end;
	}
	swr_init(pConvertCtx);


	// 打开解码器
	iRet = avcodec_open2(pCodecCtx, pCodec, NULL);
	if (iRet)
	{
		PRINT_FFMPEG_ERROR("avcodec_open2", iRet);

		goto end;
	}

	pBuf = (unsigned char*)malloc(iFrameSize);

	if (NULL == pBuf)
	{
		fprintf(stderr, "allocate failed\n");

		goto end;
	}

	// SDL初始化
	pTemp = av_get_sample_fmt_name(pCodecCtx->sample_fmt);
	printf("audio sample format is %s\n",pTemp);
	if (0 != SDL_Init(SDL_INIT_AUDIO))
	{
		fprintf(stderr, "SDL init failed\n");

		goto end;
	}
	SDL_zero(wanted);
	SDL_zero(real);
	wanted.channels = iChannelCount;
	// 特别注意，SDL只能处理packed格式的音频，如果是planar，必须重采样
	wanted.format = AUDIO_S16SYS;
	wanted.freq = iFrequency;
	// 这个和原格式保持一致
	wanted.samples = pCodecCtx->frame_size;
	wanted.silence = 0;
	wanted.callback = FillAudio;
	dev = SDL_OpenAudioDevice(NULL, 0, &wanted, &real, 0);
	if (0 == dev)
	{
		fprintf(stderr, "could not open audio device\n");

		goto end;
	}

	// 初始化同步对象
	hEvent1 = CreateEvent(NULL, TRUE, FALSE, NULL);
	hEvent2 = CreateEvent(NULL, TRUE, FALSE, NULL);
	if ((NULL == hEvent1) || (NULL == hEvent2))
	{
		fprintf(stderr, "Could not create event\n");

		goto end;
	}

	//解码
	pPkt = av_packet_alloc();
	if (NULL == pPkt)
	{
		fprintf(stderr, "av_packet_alloc failed\n");

		goto end;
	}
	pFrame = av_frame_alloc();
	if (NULL == pFrame)
	{
		fprintf(stderr, "av_frame_alloc failed\n");

		goto end;
	}

	while(true)
	{
		iRet = av_read_frame(pFmtCtx, pPkt);
		// av_read_frame返回小于0说明到达文件末尾了
		if (iRet < 0)
		{
			PRINT_FFMPEG_ERROR("av_read_frame faild:",iRet);
			break;
		}

		// 这里我们只处理音频
		if (iAudioIndex != pPkt->stream_index)
			continue;
		iRet = DecodePacket(pCodecCtx, pPkt);
		av_packet_unref(pPkt);
		if (iRet < 0)
		{
			fprintf(stderr, "unknown error\n");
			break;
		}
	}

	// flush
	DecodePacket(pCodecCtx, NULL);

end:// 资源释放
	if (pBuf)
	{
		free(pBuf);
		pBuf = NULL;
	}
	if (pOutAfter)
	{
		fclose(pOutAfter);
		pOutAfter = NULL;
	}
	if (pOutBefore)
	{
		fclose(pOutBefore);
		fclose(pOutBefore);
	}

	if (hEvent1)
	{
		CloseHandle(hEvent1);
		hEvent1 = NULL;
	}
	if (hEvent2)
	{
		CloseHandle(hEvent2);
		hEvent2 = NULL;
	}

	swr_free(&pConvertCtx);

	av_frame_free(&pFrame);
	av_packet_free(&pPkt);

	SDL_CloseAudioDevice(dev);
	SDL_Quit();

	avcodec_free_context(&pCodecCtx);
	avformat_close_input(&pFmtCtx);

	return 0;
}