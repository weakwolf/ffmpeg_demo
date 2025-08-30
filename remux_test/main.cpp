/**
 * @file transform mp4 to flv format. No encoding.
 */

#include <cstdio>

extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
}

#define PRINT_FFMPEG_ERROR(info,ret) char buf[128] = {0};\
							av_strerror(ret,buf,128);\
							fprintf(stderr,"%s:%s\n",info,buf);

#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avutil.lib")

int main(int argc, char* argv[])
{
	if (3 != argc)
	{
		fprintf(stderr, "Invalid parameters!\n");
		return -1;
	}
	if (!argv[1])
	{
		fprintf(stderr, "Invalid input file path\n");
		return -1;
	}
	if (!argv[2])
	{
		fprintf(stderr, "Invalid output file path\n");
		return -1;
	}

	int iRet = -1;
	AVFormatContext* pInputFmt = NULL;
	AVFormatContext* pOutputFmt = NULL;
	AVStream* pInputStream = NULL;
	AVStream* pOutputStream = NULL;
	AVPacket* pPkt = NULL;

	iRet = avformat_open_input(&pInputFmt, argv[1], NULL, NULL);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("Open input file failed", iRet);
		return -1;
	}
	iRet = avformat_find_stream_info(pInputFmt, NULL);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("Find stream info failed\n", iRet);
		goto end;
	}
	// ������������ļ���Ϊһ��������ַ������rtmp��ַ�����������ͱ����һ����������
	iRet = avformat_alloc_output_context2(&pOutputFmt, NULL, NULL, argv[2]);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("Open output file failed\n", iRet);
		goto end;
	}
	// ���������������������ע���ʱ������һһ��Ӧ��
	for (int i = 0; i < pInputFmt->nb_streams; ++i)
	{
		pOutputStream = avformat_new_stream(pOutputFmt, NULL);
		if (!pOutputStream)
		{
			fprintf(stderr, "Create stream failed\n");
			goto end;
		}
		// ����Դ���ı�������
		iRet = avcodec_parameters_copy(pOutputStream->codecpar, pInputFmt->streams[i]->codecpar);
		if (iRet < 0)
		{
			PRINT_FFMPEG_ERROR("Copy codec parameters failed", iRet);
			goto end;
		}
		pOutputStream->codecpar->codec_tag = 0;
	}
	// ������ļ�
	if (!(pOutputFmt->oformat->flags & AVFMT_NOFILE))
	{
		iRet = avio_open(&pOutputFmt->pb, argv[2], AVIO_FLAG_WRITE);
		if (iRet < 0)
		{
			PRINT_FFMPEG_ERROR("avio_open failed\n", iRet);
			goto end;
		}
	}

	// д���ݰ����ļ�
	pPkt = av_packet_alloc();
	if (!pPkt)
	{
		fprintf(stderr, "allocate packet error\n");
		goto end;
	}
	// д�ļ�ͷ
	iRet = avformat_write_header(pOutputFmt, NULL);
	if (iRet < 0)
	{
		PRINT_FFMPEG_ERROR("Write file header failed\n", iRet);
		goto end;
	}
	while (true)
	{
		iRet = av_read_frame(pInputFmt, pPkt);
		if (iRet < 0)
		{
			if (AVERROR_EOF == AVERROR(iRet))
			{
				fprintf(stderr, "End of the input file\n");
				break;
			}
			else
			{
				fprintf(stderr, "av_read_frame failed\n");
				break;
			}
		}
		printf("input packet:%lld\n", pPkt->pts);
		pInputStream = pInputFmt->streams[pPkt->stream_index];
		pOutputStream = pOutputFmt->streams[pPkt->stream_index];
		// ʱ���ת����ע��������û�ͬʱ����dts��pts
		av_packet_rescale_ts(pPkt, pInputStream->time_base, pOutputStream->time_base);
		printf("output packet:%lld\n", pPkt->pts);
		av_interleaved_write_frame(pOutputFmt, pPkt);
		av_packet_unref(pPkt);
	}
	// д�ļ�β
	av_write_trailer(pOutputFmt);

end:
	if (!(pOutputFmt->oformat->flags & AVFMT_NOFILE))
		avio_closep(&pOutputFmt->pb);
	av_packet_free(&pPkt);
	avformat_close_input(&pInputFmt);
	avformat_free_context(pOutputFmt);

	return 0;
}