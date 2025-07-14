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

//#define OUTPUT_BEFORE	// �Ƿ񽫽���󣬻�û���ز���pcm���ݱ��浽����
//#define OUTPUT_AFTER	// �Ƿ񽫽�����ز���pcm���ݱ��浽����

#define MAX_BUFFER_SIZE_MP3	2 * 2 * 48000	// 1s����Ƶ���ݴ�С

static const char* pInput = NULL;
static AVFormatContext* pFmtCtx = NULL;
static AVCodecContext* pCodecCtx = NULL;
static const AVCodec* pCodec = NULL;
static int iAudioIndex = -1;	// ��Ƶ������
static AVPacket* pPkt = NULL;
static AVFrame* pFrame = NULL;

static const char* pTemp = NULL;

static SDL_AudioSpec wanted;	// ��������Ƶ��ʽ
static SDL_AudioSpec real;		// ʵ�ʵ���Ƶ��ʽ
static SDL_AudioDeviceID dev;

static int iFrameSize = -1;
static unsigned char* pBuf = NULL;	// �洢�ز��������Ƶ֡����
static HANDLE hEvent1 = NULL;	// ���ڽ����߳�֪ͨsdl�����߳���������
static HANDLE hEvent2 = NULL;	// ����sdl�����߳�֪ͨ�����̲߳�����ɿ��Կ�ʼ������һ֡��
static bool bStart = false;	// SDL�Ƿ��Ѿ���ʼ����

static int iChannelCount = -1;
static int iFrequency = -1;
static AVSampleFormat inSampleFmt = AV_SAMPLE_FMT_NONE;	// ������Ƶ������ʽ
static AVSampleFormat outSampleFmt = AV_SAMPLE_FMT_S16; // SDL��������Ƶ������ʽ
static SwrContext* pConvertCtx = NULL;
static AVChannelLayout outChannelLayout;	// ����������֣��°汾��ffmpegҪ��������ṹ�壬����ʹ���������ͣ���ô�о�Խ��Խ������
static AVChannelLayout inChannelLayout;		// ������������

static FILE* pOutAfter = NULL;
static FILE* pOutBefore = NULL;
static char pOutPath[260] = { 0 };


static void FillAudio(void* opaque, Uint8* pStream, int iLen)
{
	// �������Ҫ�ڻص��������汣֤�̰߳�ȫ������SDL_LockAudioDevice
	// ���Ǵ���ģ��ûص������ڵ���֮ǰĬ�ϻ���øú�������֤�̰߳�ȫ

	if (0 == iLen)
		return;

	WaitForSingleObject(hEvent1, 1000);
	printf("sdl play one frame\n");
	// SDL2�����ȵ���SDL_memset��ջ���������������Բ���������
	SDL_memset(pStream, 0, iLen);
	iLen = iLen > iFrameSize ? iFrameSize : iLen;
	// �������ַ�ʽ������
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
			// û�и������֡������Ҫ�������ݲ��ܽ���
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
			// ���չٷ���˵�������������������ʹ��extended_data���������ݣ����ٷ���׼û��
			// ��ʱ��linesize[0]��һ��ͨ������Ƶ���ݵĴ�С������ͨ���Ĵ�СҲ�������������������ֵ����0
			// �Һ��ɻ�Ϊʲô������ͨ��һ��д��ȥ���žͲ��ԣ�д����ͨ������������
			fwrite(pFrame->extended_data[0], 1, pFrame->linesize[0], pOutBefore);
			//fwrite(pFrame->extended_data[1], 1, pFrame->linesize[0], pOutBefore);
			//printf("%p",pFrame->extended_data[1]);
		}
#endif

		swr_convert(pConvertCtx, &pBuf, pFrame->nb_samples, (const uint8_t**)pFrame->data, pFrame->nb_samples);
		if (!bStart)
		{
			// ȷ��ffmpeg�ɹ�����֮������sdl��ʼ����
			SDL_PauseAudioDevice(dev, 0);
			bStart = true;
		}

		printf("notify sdl\n");
		// ֪ͨsdl�ص�����Ƶ������
		SetEvent(hEvent1);
		// �ȴ�sdl�ص�����������
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

	// ��ʼ��
	int iRet = avformat_open_input(&pFmtCtx, pInput, NULL, NULL);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("avformat_open_input failed", iRet);

		goto end;
	}
	// ̽������Ϣ
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
	// ��ʼ��������
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

	// �ز������
	iChannelCount = pCodecCtx->ch_layout.nb_channels;
	iFrequency = pCodecCtx->sample_rate;
	inSampleFmt = pCodecCtx->sample_fmt;
	iFrameSize = av_samples_get_buffer_size(NULL, iChannelCount, pCodecCtx->frame_size, outSampleFmt, 1);
	
	// ���������AV_SAMPLE_FMT_FLTP���ͣ�ҪתΪAV_SAMPLE_FMT_S16��SDL���ܲ���
	// ��������������
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


	// �򿪽�����
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

	// SDL��ʼ��
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
	// �ر�ע�⣬SDLֻ�ܴ���packed��ʽ����Ƶ�������planar�������ز���
	wanted.format = AUDIO_S16SYS;
	wanted.freq = iFrequency;
	// �����ԭ��ʽ����һ��
	wanted.samples = pCodecCtx->frame_size;
	wanted.silence = 0;
	wanted.callback = FillAudio;
	dev = SDL_OpenAudioDevice(NULL, 0, &wanted, &real, 0);
	if (0 == dev)
	{
		fprintf(stderr, "could not open audio device\n");

		goto end;
	}

	// ��ʼ��ͬ������
	hEvent1 = CreateEvent(NULL, TRUE, FALSE, NULL);
	hEvent2 = CreateEvent(NULL, TRUE, FALSE, NULL);
	if ((NULL == hEvent1) || (NULL == hEvent2))
	{
		fprintf(stderr, "Could not create event\n");

		goto end;
	}

	//����
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
		// av_read_frame����С��0˵�������ļ�ĩβ��
		if (iRet < 0)
		{
			PRINT_FFMPEG_ERROR("av_read_frame faild:",iRet);
			break;
		}

		// ��������ֻ������Ƶ
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

end:// ��Դ�ͷ�
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