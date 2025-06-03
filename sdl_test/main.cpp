#include "SDL2/SDL.h"

#include <cstdio>

#ifdef _DEBUG
#pragma comment(lib,"SDL2d.lib")
#pragma comment(lib,"manual-link/SDL2maind.lib")
#else
#pragma comment(lib,"SDL2.lib")
#pragma comment(lib,"manual-link/SDL2main.lib")
#endif

int main(int argc,char* argv[])
{
	int iWidth = 1920;
	int iHeight = 1080;

	SDL_Window* pMainWin = NULL;
	SDL_Renderer* pRenderer = NULL;
	SDL_Texture* pTexture = NULL;
	SDL_Rect rect;
	FILE* pIn = NULL;
	unsigned char* pBuf = NULL;

	// 初始化
	int iRet = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
	if (0 != iRet)
	{
		fprintf(stderr, "Could not init SDL,error:%s", SDL_GetError());

		return -1;
	}
	// 创建窗口
	pMainWin = SDL_CreateWindow("sdl test",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,iWidth,iHeight,SDL_WINDOW_OPENGL);
	if (NULL == pMainWin)
	{
		fprintf(stderr, "Could not create sdl windows, error:%s", SDL_GetError());

		goto end;
	}
	// 创建画笔
	pRenderer = SDL_CreateRenderer(pMainWin, -1, 0);
	if (!pRenderer)
	{
		fprintf(stderr, "Could not create sdl renderer, error:%s", SDL_GetError());

		goto end;
	}
	// 创建纹理,yuv420p
	pTexture = SDL_CreateTexture(pRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, iWidth, iHeight);
	if (!pTexture)
	{
		fprintf(stderr, "Could not create sdl texture, error:%s", SDL_GetError());

		goto end;
	}
	rect.x = 0;
	rect.y = 0;
	rect.w = iWidth;
	rect.h = iHeight;

	pIn = fopen("../resource/wcr_yuv_420p.yuv", "rb");
	pBuf = (unsigned char*)malloc(iHeight * iWidth * 3 / 2);
	fread(pBuf, 1, iWidth*iHeight * 3 / 2, pIn);

	SDL_UpdateTexture(pTexture, &rect, pBuf, 1920);
	// 清空原有渲染内容
	SDL_RenderClear(pRenderer);
	SDL_RenderCopy(pRenderer, pTexture, NULL, &rect);
	SDL_RenderPresent(pRenderer);

	SDL_Delay(10000);


end:
	if (pIn)
		fclose(pIn);
	if (pBuf)
		free(pBuf);
	if (pTexture)
		SDL_DestroyTexture(pTexture);
	if (pRenderer)
		SDL_DestroyRenderer(pRenderer);
	if(pMainWin)
		SDL_DestroyWindow(pMainWin);
	SDL_Quit();

	return 0;
}