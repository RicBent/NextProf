#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <3ds.h>

#include "http.h"

inline const char* rStr(Result r) { return R_FAILED(r) ? "FAIL" : "OK"; };

const u64 SYS_TITLE_ID = 0x00040130091A8C02ULL;

int main()
{
	Result r;
	bool launched = false;
	u32 launchPid = 0;
	int launchResult = 0;
	u32 downloaded_size = 0;

	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	printf("NextProf\n\n");

	nsInit();
	httpcInit(0);

	printf("Downloading sysmodule files...\n");
	gspWaitForVBlank();
	gfxSwapBuffers();

	r = http_download(HTTP_HOST "/sysmodule/code.bin", "/luma/titles/00040130091A8C02/code.bin", &downloaded_size);
	printf("code.bin:     %s (%lu bytes)\n", rStr(r), downloaded_size);
	gspWaitForVBlank();
	gfxSwapBuffers();

	r = http_download(HTTP_HOST "/sysmodule/exheader.bin", "/luma/titles/00040130091A8C02/exheader.bin", &downloaded_size);
	printf("exheader.bin: %s (%lu bytes)\n\n", rStr(r), downloaded_size);

	printf("Press A to start profiler sysmodule.\nPress START to exit.\n\n");

	while (aptMainLoop())
	{
		gspWaitForVBlank();
		gfxSwapBuffers();
		hidScanInput();

		u32 kDown = hidKeysDown();

		if (kDown & KEY_A)
		{
			launchResult = NS_LaunchTitle(SYS_TITLE_ID, 0, &launchPid);
			launched = true;
			break;
		}
		else if (kDown & KEY_START)
			break;
	}

	if (launched && R_FAILED(launchResult))
	{
		printf("Failed to launch profiler sysmodule: %08X\n", launchResult);
		
		while (aptMainLoop())
		{
			gspWaitForVBlank();
			gfxSwapBuffers();
			hidScanInput();

			u32 kDown = hidKeysDown();

			if (kDown & KEY_START)
				break;
		}
	}

	httpcExit();
	nsExit();
	gfxExit();

	return 0;
}
