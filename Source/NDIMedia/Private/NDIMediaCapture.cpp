// Fill out your copyright notice in the Description page of Project Settings.


#include "NDIMediaCapture.h"
#include "NDIMediaOutput.h"

#include <chrono>
#include <vector>

struct FNdiFrameBuffer
{
	NDIlib_video_frame_v2_t Frame;
	std::vector<uint8_t> Buffer;
};

UNDIMediaCapture::UNDIMediaCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UNDIMediaCapture::HasFinishedProcessing() const
{
	return Super::HasFinishedProcessing();
}

bool UNDIMediaCapture::ValidateMediaOutput() const
{
	UNDIMediaOutput* Output = CastChecked<UNDIMediaOutput>(MediaOutput);
	check(Output);
	return true;
}

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
bool UNDIMediaCapture::InitializeCapture()
{
	//if (!Super::InitializeCapture())
	//	return false;

	UNDIMediaOutput* Output = CastChecked<UNDIMediaOutput>(MediaOutput);
	OutputPixelFormat = Output->OutputPixelFormat;
	return InitNdi(Output);
}

#else

bool UNDIMediaCapture::CaptureSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	UNDIMediaOutput* Output = CastChecked<UNDIMediaOutput>(MediaOutput);
	OutputPixelFormat = Output->OutputPixelFormat;
	OutputFrameRate = Output->OutputFrameRate;
	return InitNDI(Output);
}

bool UNDIMediaCapture::CaptureRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
	return false;
}

#endif

bool UNDIMediaCapture::UpdateSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	return false;
}

bool UNDIMediaCapture::UpdateRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
	return false;
}

void UNDIMediaCapture::StopCaptureImpl(bool bAllowPendingFrameToBeProcess)
{
	Super::StopCaptureImpl(bAllowPendingFrameToBeProcess);
	DisposeNdi();
}

void UNDIMediaCapture::OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData,
	TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height,
	int32 BytesPerRow)
{
	FNdiFrameBuffer* FrameBuffer = new FNdiFrameBuffer();

	FrameBuffer->Frame.frame_rate_N = OutputFrameRate.Numerator;
	FrameBuffer->Frame.frame_rate_D = OutputFrameRate.Denominator;
	
	if (OutputPixelFormat == ENDIMediaOutputPixelFormat::NDI_PF_P210)
	{
		NDIlib_video_frame_v2_t VideoFrameV210;
		VideoFrameV210.line_stride_in_bytes = Width * 16;
		VideoFrameV210.xres = Width * 6;
		VideoFrameV210.yres = Height;
		VideoFrameV210.FourCC = NDIlib_FourCC_type_P216;
		VideoFrameV210.p_data = (uint8_t*)InBuffer;
	
		NDIlib_video_frame_v2_t& VideoFrameV216 = FrameBuffer->Frame;
		VideoFrameV216.line_stride_in_bytes = VideoFrameV210.xres * sizeof(uint16_t) * 4;
	
		FrameBuffer->Buffer.resize(VideoFrameV216.line_stride_in_bytes * VideoFrameV210.yres * sizeof(uint16_t));
		
		VideoFrameV216.p_data = &FrameBuffer->Buffer[0];
	
		NDIlib_util_V210_to_P216(&VideoFrameV210, &VideoFrameV216);
	}
	else if (OutputPixelFormat == ENDIMediaOutputPixelFormat::NDI_PF_RGB)
	{
		const uint8_t* Ptr = (const uint8_t*)InBuffer;
		FrameBuffer->Buffer.assign(Ptr, Ptr + (BytesPerRow * Height));
		
		NDIlib_video_frame_v2_t& VideoFrameRgb = FrameBuffer->Frame;
		VideoFrameRgb.line_stride_in_bytes = Width * 4;
		VideoFrameRgb.xres = Width;
		VideoFrameRgb.yres = Height;
		VideoFrameRgb.FourCC = NDIlib_FourCC_type_BGRA; // NDIlib_FourCC_type_BGRX;
		VideoFrameRgb.p_data = &FrameBuffer->Buffer[0];
	}

	{
		FScopeLock ScopeLock(&RenderThreadCriticalSection);
		FrameBuffers.push_back(FrameBuffer);
	}
}

bool UNDIMediaCapture::InitNdi(UNDIMediaOutput* Output)
{
	FScopeLock ScopeLock(&RenderThreadCriticalSection);

	NDIlib_send_create_t Settings;
	Settings.p_ndi_name = TCHAR_TO_ANSI(*Output->SourceName);
	pNDI_send = NDIlib_send_create(&Settings);

	if (!pNDI_send)
	{
		SetState(EMediaCaptureState::Error);
		return false;
	}

	SetState(EMediaCaptureState::Capturing);

	///
	
	const int N = 4;
	FrameBuffers.resize(N);

	for (int I = 0; I < FrameBuffers.size(); I++)
		FrameBuffers[I] = new FNdiFrameBuffer();

	NDISendThreadRunning = true;
	NDISendThread = std::thread([this]()
	{
		while (this->NDISendThreadRunning)
		{
			bool bHasQueue = false;

			{
				FScopeLock ScopeLock(&this->RenderThreadCriticalSection);
				bHasQueue = this->FrameBuffers.size() > 0;
			}

			if (bHasQueue)
			{
				FNdiFrameBuffer* FrameBuffer = nullptr;
				{
					FScopeLock ScopeLock(&this->RenderThreadCriticalSection);
					FrameBuffer = this->FrameBuffers.front();
					this->FrameBuffers.pop_front();
				}

				NDIlib_send_send_video_v2(pNDI_send, &FrameBuffer->Frame);
				
				delete FrameBuffer;
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	});
	
	return true;
}

bool UNDIMediaCapture::DisposeNdi()
{
	FScopeLock ScopeLock(&RenderThreadCriticalSection);

	NDISendThreadRunning = false;
	NDISendThread.join();
	
	if (pNDI_send)
	{
		NDIlib_send_destroy(pNDI_send);
		pNDI_send = nullptr;
	}

	for (int I = 0; I < FrameBuffers.size(); I++)
		delete FrameBuffers[I];

	FrameBuffers.clear();
	
	return true;
}
