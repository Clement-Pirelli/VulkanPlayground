#pragma once

struct RENDERDOC_API_1_4_1;


class RenderDoc
{
public:
	
	RenderDoc();
	void startCapture();
	void stopCapture();
	void triggerCapture();

private:

	void stopCaptureInternal();

	bool initDone = false;
	RENDERDOC_API_1_4_1 *api = nullptr;
};

