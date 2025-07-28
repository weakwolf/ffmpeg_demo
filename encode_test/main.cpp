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

	// 我们这里已经提前知道宽高，但是实际编码中应该是从某处获得
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
	// 设置编码器参数，以下这些参数应该设置，ffmpeg官方的案例就设置了这些参数
	pCodecCtx->width = iWidth;
	pCodecCtx->height = iHeight;
	pCodecCtx->bit_rate = 400000;
	// 时间基和帧率，非常重要，必须设置
	pCodecCtx->time_base = { 1,25 };
	pCodecCtx->framerate = { 25,1 };
	pCodecCtx->gop_size = 10;
	pCodecCtx->max_b_frames = 1;
	// 我们的输入文件是yuv420p数据
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	// priv_data是编码器的私有选项，这里对于h264编码器
	// preset是一个预设选项，用于控制编码速度和编码效率之间的平衡
	if (AV_CODEC_ID_H264 == pCodecCtx->codec_id)
		av_opt_set(pCodecCtx->priv_data, "preset", "slow", 0);
	// 打开解码器
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

	// 编码
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
	// 设置AVFrame的参数，对于解码，这些参数不需要我们设置
	// 编码这些参数都要我们自己设置，相对来说要麻烦一点
	pFrame->width = pCodecCtx->width;
	pFrame->height = pCodecCtx->height;
	pFrame->format = pCodecCtx->pix_fmt;
	// 分配AVFrame的内存，我们的裸数据没有字节对齐，所以第二个参数设为1
	av_frame_get_buffer(pFrame, 1);
	while (!feof(pIn))
	{
		fread(pFrame->data[0], 1, iWidth * iHeight, pIn);
		fread(pFrame->data[1], 1, iWidth * iHeight / 4, pIn);
		fread(pFrame->data[2], 1, iWidth * iHeight / 4, pIn);
		// *1000是转换为ms
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
					goto end;	// 这种情况只可能是出现错误了
			}

			fwrite(pPkt->data, 1, pPkt->size, pOut);
			printf("packet--pts=%d,dts=%d\n", pPkt->pts, pPkt->dts);
			av_packet_unref(pPkt);

			// 你可能会疑惑为什么不调用av_frame_unref
			// 因为我们这里是自己构造的AVFrame，它的内存指针是不能变的
			// 如果调用了avcodec_send_frame，这个指针就被重置了
		}
	}
	// flush encoder，这一步不要忘了
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
				goto end;	// 这种情况只可能是出现错误了
		}

		fwrite(pPkt->data, 1, pPkt->size, pOut);
		printf("packet--pts=%d,dts=%d\n", pPkt->pts, pPkt->dts);
		av_packet_unref(pPkt);
	}


end:// 资源释放
	if (pIn) fclose(pIn);
	if (pOut) fclose(pOut);

	av_frame_free(&pFrame);
	av_packet_free(&pPkt);
	avcodec_free_context(&pCodecCtx);

	return 0;
}