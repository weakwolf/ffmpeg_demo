#pragma once

#include <cstdio>

// functions
void Log(const char* pInfo = NULL);





// macros
#define PRINT_FFMPEG_ERROR(info,ret) char buf[128] = {0};\
							av_strerror(ret,buf,128);\
							printf("%s:%s\n",info,buf);	

#define IN
#define OUT