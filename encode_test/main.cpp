#include <cstdio>

extern "C"
{
#include "libavcodec/avcodec.h"
#include <libavutil/opt.h>
}

#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avutil.lib")

#define PRINT_FFMPEG_ERROR(info,ret) char buf[128] = {0};\
							av_strerror(ret,buf,128);\
							fprintf(stderr,"%s:%s\n",info,buf);


int main(int argc, char* argv[])
{
	//printf("hello ffmpeg\n");

	// ���������Ѿ���ǰ֪����ߣ�����ʵ�ʱ�����Ӧ���Ǵ�ĳ�����
	int iWidth = 480;
	int iHeight = 272;
	const char* pInputFile = argv[1];
	const char* pOutputFile = argv[2];
	if (NULL == pInputFile || NULL == pOutputFile)
	{
		fprintf(stderr, "invalid parameters\n");
		return -1;
	}

	AVCodecContext* pCodecCtx = NULL;
	const AVCodec* pEncoder = NULL;
	AVFrame* pFrame = NULL;
	AVPacket* pPkt = NULL;
	int iFrameIndex = 0;

	FILE* pIn = NULL;
	FILE* pOut = NULL;


	pEncoder = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (NULL == pEncoder)
	{
		fprintf(stderr, "Could not find h264 encoder\n");
		return -1;
	}
	pCodecCtx = avcodec_alloc_context3(pEncoder);
	if (NULL == pCodecCtx)
	{
		fprintf(stderr, "avcodec_alloc_context3\n");
		return -1;
	}
	// ���ñ�����������������Щ����Ӧ�����ã�ffmpeg�ٷ��İ�������������Щ����
	pCodecCtx->width = iWidth;
	pCodecCtx->height = iHeight;
	pCodecCtx->bit_rate = 400000;
	// ʱ�����֡�ʣ��ǳ���Ҫ����������
	pCodecCtx->time_base = { 1,25 };
	pCodecCtx->framerate = { 25,1 };
	pCodecCtx->gop_size = 10;
	pCodecCtx->max_b_frames = 1;
	// ���ǵ������ļ���yuv420p����
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	// priv_data�Ǳ�������˽��ѡ��������h264������
	// preset��һ��Ԥ��ѡ����ڿ��Ʊ����ٶȺͱ���Ч��֮���ƽ��
	if (AV_CODEC_ID_H264 == pCodecCtx->codec_id)
		av_opt_set(pCodecCtx->priv_data, "preset", "slow", 0);
	// �򿪽�����
	int iRet = avcodec_open2(pCodecCtx, pEncoder, NULL);
	if(iRet < 0)
	{
		PRINT_FFMPEG_ERROR("Could not open encoder", iRet);
		goto end;
	}

	pIn = fopen(pInputFile, "rb");
	if (NULL == pIn)
	{
		fprintf(stderr, "Could not open %s\n", pInputFile);
		goto end;
	}
	pOut = fopen(pOutputFile, "wb+");
	if (NULL == pOut)
	{
		fprintf(stderr, "Could not open %s\n", pOutputFile);
		goto end;
	}

	// ����
	pFrame = av_frame_alloc();
	if (NULL == pFrame)
	{
		fprintf(stderr, "avframe_alloc error\n");
		goto end;
	}
	pPkt = av_packet_alloc();
	if (NULL == pPkt)
	{
		fprintf(stderr, "av_packet_alloc error\n");
		goto end;
	}
	// ����AVFrame�Ĳ��������ڽ��룬��Щ��������Ҫ��������
	// ������Щ������Ҫ�����Լ����ã������˵Ҫ�鷳һ��
	pFrame->width = pCodecCtx->width;
	pFrame->height = pCodecCtx->height;
	pFrame->format = pCodecCtx->pix_fmt;
	// ����AVFrame���ڴ棬���ǵ�������û���ֽڶ��룬���Եڶ���������Ϊ1
	av_frame_get_buffer(pFrame, 1);
	while (!feof(pIn))
	{
		fread(pFrame->data[0], 1, iWidth * iHeight, pIn);
		fread(pFrame->data[1], 1, iWidth * iHeight / 4, pIn);
		fread(pFrame->data[2], 1, iWidth * iHeight / 4, pIn);
		// *1000��ת��Ϊms
		pFrame->pts = iFrameIndex / av_q2d(pCodecCtx->framerate) / av_q2d(pCodecCtx->time_base);
		++iFrameIndex;
		iRet = avcodec_send_frame(pCodecCtx, pFrame);
		if (iRet < 0)
		{
			PRINT_FFMPEG_ERROR("avcodec_send_frame error", iRet);
			goto end;
		}
		while (iRet >= 0)
		{
			iRet = avcodec_receive_packet(pCodecCtx, pPkt);
			if (iRet < 0)
			{
				if (AVERROR_EOF == iRet || AVERROR(EAGAIN) == iRet)
					break;
				else
					goto end;	// �������ֻ�����ǳ��ִ�����
			}

			fwrite(pPkt->data, 1, pPkt->size, pOut);
			printf("packet--pts=%d,dts=%d\n", pPkt->pts, pPkt->dts);
			av_packet_unref(pPkt);

			// ����ܻ��ɻ�Ϊʲô������av_frame_unref
			// ��Ϊ�����������Լ������AVFrame�������ڴ�ָ���ǲ��ܱ��
			// ���������avcodec_send_frame�����ָ��ͱ�������
		}
	}
	// flush encoder����һ����Ҫ����
	iRet = avcodec_send_frame(pCodecCtx, NULL);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("avcodec_send_frame error", iRet);
		goto end;
	}
	while (iRet >= 0)
	{
		iRet = avcodec_receive_packet(pCodecCtx, pPkt);
		if (iRet < 0)
		{
			if (AVERROR_EOF == iRet || AVERROR(EAGAIN) == iRet)
				break;
			else
				goto end;	// �������ֻ�����ǳ��ִ�����
		}

		fwrite(pPkt->data, 1, pPkt->size, pOut);
		printf("packet--pts=%d,dts=%d\n", pPkt->pts, pPkt->dts);
		av_packet_unref(pPkt);
	}


end:// ��Դ�ͷ�
	if (pIn) fclose(pIn);
	if (pOut) fclose(pOut);

	av_frame_free(&pFrame);
	av_packet_free(&pPkt);
	avcodec_free_context(&pCodecCtx);

	return 0;
}