#include "utils.h"

void Log(const char* pInfo /* = NULL */)
{
	printf("[simple player error] (%s) %s:%d\n", pInfo, __FILE__, __LINE__);
}

