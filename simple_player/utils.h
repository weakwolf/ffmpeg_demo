#pragma once

#include <cstdio>

// functions
void Log(const char* pInfo,const char* pFileName,int iLine);





// macros
#define PRINT_FFMPEG_ERROR(info,ret) char buf[128] = {0};\
							av_strerror(ret,buf,128);\
							printf("%s:%s\n",info,buf);	
#define SAFE_POINTER_CALL(p,expr) if(p) expr;

#define IN
#define OUT