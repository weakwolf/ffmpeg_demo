/**
 * @file libavcodec audio encode API usage examples
 *
 * encode pcm data to AAC
 */

#include <cstdio>
#include <cstdlib>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"
#include "libavutil/avutil.h"
}

#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"avutil.lib")

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
#define SAFE_CLOSE(p) if(p) {\
						fclose(p);\
						p = NULL;\
						}

static int iRet = 0;

FILE* pInput = NULL;
FILE* pOutput = NULL;

static const AVCodec* pCodec = NULL;			// 编码器ID	
static AVCodecContext* pCodecCtx = NULL;		// 编码上下文
static AVCodecID codec = AV_CODEC_ID_NONE;		// 编码器ID
static AVSampleFormat inputFmt = AV_SAMPLE_FMT_NONE;	// 输入音频格式
static AVSampleFormat outputFmt = AV_SAMPLE_FMT_NONE;	// 输出音频格式
static AVFrame* pFrame = NULL;		// 输入音频帧
static AVFrame* pOutFrame = NULL;	// 重采样后的音频帧
static AVPacket* pPacket = NULL;
static int iFrameSize = 0;	// 对于待编码数据，一帧音频大小

static SwrContext* pConvertCtx = NULL;
static AVChannelLayout inChannelLayout;	// 输入声道布局


// 判断某个音频编码器是否支持指定音频格式
static bool CheckSampleFmt(const AVCodec* pCodec, AVSampleFormat fmt)
{
	const AVSampleFormat* pFmt = pCodec->sample_fmts;
	while (*pFmt != AV_SAMPLE_FMT_NONE)
	{
		if (*pFmt == fmt)
			return true;
		++pFmt;
	}
	return false;
}

// 选择一个最合适，并且编码器支持的采样率，这里只是选择和44100最接近的采样率
static int SelectSampleRate(const AVCodec* pCodec)
{
	int iBestRate = 0;
	if (!pCodec->supported_samplerates)
		return 44100;

	const int* pRate = pCodec->supported_samplerates;
	while (*pRate)
	{
		if (!iBestRate || abs(44100 - *pRate) < abs(44100 - iBestRate))
			iBestRate = *pRate;
		++pRate;
	}

	return iBestRate;
}

static int EncodeAudio(AVCodecContext* pCodecCtx, AVFrame* pFrame, AVPacket* pPacket)
{
	iRet = avcodec_send_frame(pCodecCtx, pFrame);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("avcodec_send_frame error", iRet);
		return -1;
	}

	while (true)
	{
		iRet = avcodec_receive_packet(pCodecCtx, pPacket);
		if (iRet < 0)
		{
			if (AVERROR_EOF == iRet || AVERROR(EAGAIN) == iRet)
			{
				printf("need more frame to encode\n");
				return 0;
			}
			PRINT_FFMPEG_ERROR("avcodec_receive_packet error\n", iRet);
			return -1;
		}

		fwrite(pPacket->data, 1, pPacket->size, pOutput);
		fprintf(stderr, "encode one frame,size:%d\n", pPacket->size);
		av_packet_unref(pPacket);
	}
}

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		fprintf(stderr, "Invalid parameters, please check\n");
		return -1;
	}
	if (!argv[1])
	{
		fprintf(stderr, "No input file %s\n",argv[1]);
		return -1;
	}
	if (!argv[2])
	{
		fprintf(stderr, "No output file %s\n",argv[2]);
		return -1;
	}

	// 编码初始化
	codec = AV_CODEC_ID_AAC;
	pCodec = avcodec_find_encoder(codec);
	if (!pCodec)
	{
		fprintf(stderr, "Could not open %s encoder\n", avcodec_get_name(codec));
		return -1;
	}
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (!pCodecCtx)
	{
		fprintf(stderr, "avcodec_alloc_context3 failed\n");
		return -1;
	}
	inputFmt = AV_SAMPLE_FMT_S16;
	outputFmt = AV_SAMPLE_FMT_FLTP;
	pCodecCtx->bit_rate = 64000;
	if (!CheckSampleFmt(pCodec, outputFmt))
	{
		fprintf(stderr, "%s could not support %s\n", avcodec_get_name(codec), av_get_sample_fmt_name(outputFmt));
		goto end;
	}
	pCodecCtx->sample_fmt = outputFmt;	// 音频格式
	pCodecCtx->sample_rate = SelectSampleRate(pCodec);	// 采样率
	av_channel_layout_default(&pCodecCtx->ch_layout, 2);
	iRet = avcodec_open2(pCodecCtx, pCodec, NULL);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("avcodec_open2 error\n", iRet);
		goto end;
	}

	//重采样初始化，因为AAC编码器不支持s16格式的音频，需要转换
	swr_alloc_set_opts2(&pConvertCtx, &pCodecCtx->ch_layout, AV_SAMPLE_FMT_FLTP, pCodecCtx->sample_rate,
		&pCodecCtx->ch_layout, AV_SAMPLE_FMT_S16, 44100, 0, NULL);
	if (!pConvertCtx)
	{
		fprintf(stderr, "Could not create resample convert context\n");

		goto end;
	}
	swr_init(pConvertCtx);

	// 编码
	pFrame = av_frame_alloc();
	if (!pFrame)
	{
		fprintf(stderr, "av_frame_alloc failed\n");
		goto end;
	}
	pFrame->format = inputFmt;
	// 这个值是根据你的待编码的pcm数据确定的，不同的源数据这个值也不同
	// 如果你的数据是mp3解码器解码的，通常是1152，而AAC通常是1024
	pFrame->nb_samples = 1024;
	iRet = av_channel_layout_copy(&pFrame->ch_layout, &pCodecCtx->ch_layout);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("av_channel_layout_copy error\n", iRet);
		goto end;
	}
	iRet = av_frame_get_buffer(pFrame, 1);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("av_frame_get_buffer error\n", iRet);
		goto end;
	}
	pOutFrame = av_frame_alloc();
	if (!pOutFrame)
	{
		fprintf(stderr, "av_frame_alloc failed\n");
		goto end;
	}
	pOutFrame->format = outputFmt;
	pOutFrame->nb_samples = pCodecCtx->frame_size;
	iRet = av_channel_layout_copy(&pOutFrame->ch_layout, &pCodecCtx->ch_layout);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("av_channel_layout_copy error\n", iRet);
		goto end;
	}
	iRet = av_frame_get_buffer(pOutFrame, 1);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("av_frame_get_buffer error\n", iRet);
		goto end;
	}
	pPacket = av_packet_alloc();
	if (!pPacket)
	{
		fprintf(stderr, "av_packet_alloc error\n");
		goto end;
	}
	pInput = fopen(argv[1], "rb");
	if (!pInput)
	{
		fprintf(stderr, "Could not open %s", argv[1]);
		goto end;
	}
	pOutput = fopen(argv[2], "wb+");
	if (!pOutput)
	{
		fprintf(stderr, "Could not open %s", argv[2]);
		goto end;
	}
	while (!feof(pInput))
	{
		fread(pFrame->data[0], 1, pFrame->linesize[0], pInput);
		// 我发现用extended_data来访问数据就会出错？
#if 0
		pOutFrame->nb_samples = swr_convert(pConvertCtx, (uint8_t**)&pOutFrame->extended_data, 
										pOutFrame->nb_samples, (const uint8_t**)pFrame->data, pFrame->nb_samples);
#else
		pOutFrame->nb_samples = swr_convert(pConvertCtx, (uint8_t**)&pOutFrame->data,
			pOutFrame->nb_samples, (const uint8_t**)pFrame->data, pFrame->nb_samples);
#endif
		iRet = EncodeAudio(pCodecCtx, pOutFrame, pPacket);
		if (iRet < 0)
		{
			fprintf(stderr, "error occured when encoding\n");
			break;
		}
	}
	// flush encoder
	EncodeAudio(pCodecCtx, NULL, pPacket);

end:
	SAFE_CLOSE(pInput);
	SAFE_CLOSE(pOutput);
	swr_free(&pConvertCtx);
	av_frame_free(&pFrame);
	av_frame_free(&pOutFrame);
	av_packet_free(&pPacket);
	avcodec_free_context(&pCodecCtx);
	return 0;
}