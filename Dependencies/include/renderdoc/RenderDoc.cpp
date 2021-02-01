#include "RenderDoc.h"
#include "renderdoc_app.h"
#include <Windows.h>
#include <iostream>

#define INIT_CHECK(expression, errorStr) if(initDone) { expression; } else std::cerr << errorStr << ": renderDoc API was not properly initialized!\n";

RenderDoc::RenderDoc()
{
	if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
	{
		pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
		int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&api);
		if (ret == 1)
		{
			initDone = true;
		}
		else
		{
			initDone = false;
			std::cerr << "Could not initialize renderdoc!\n";
		}
	}
	else
	{
		auto a = GetLastError();
		std::cerr << "Could not get renderDoc module handle!\n" << a << '\n';
	}
}

void RenderDoc::startCapture()
{
	INIT_CHECK(api->StartFrameCapture(NULL, NULL), "Could not start frame capture");
}

void RenderDoc::stopCapture()
{
	INIT_CHECK(stopCaptureInternal(), "Could not end frame capture")
}

void RenderDoc::triggerCapture()
{
	INIT_CHECK(api->TriggerCapture(), "Could not trigger capture");
}

void RenderDoc::stopCaptureInternal()
{
	if (api->EndFrameCapture(NULL, NULL) == 1) 
	{
		std::cout << "Captured frame with renderdoc!\n";
	}
	char filename[256];
	api->GetCapture(0, filename, nullptr, nullptr);
	std::cout << "\nRenderdoc captured at : " << filename << "\n";
}
