#include "utils.h"


void Log(const char* pInfo, const char* pFileName, int iLine)
{
	printf("[simple player error] (%s) %s:%d\n", pInfo, pFileName, iLine);
}

