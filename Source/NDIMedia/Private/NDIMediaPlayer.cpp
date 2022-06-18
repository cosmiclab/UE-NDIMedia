﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "NDIMediaPlayer.h"

#include "NDIMedia.h"
#include "IMediaEventSink.h"
#include "MediaIOCoreSamples.h"

FNDIMediaPlayer::FNDIMediaPlayer(IMediaEventSink& InEventSink)
	: Super(InEventSink)
	, TextureSamplePool(new FNDIMediaTextureSamplePool())
	, MetadataSamplePool(new FNDIMediaBinarySamplePool())
{
}

FNDIMediaPlayer::~FNDIMediaPlayer()
{
	delete TextureSamplePool;
	delete MetadataSamplePool;
}

FGuid FNDIMediaPlayer::GetPlayerPluginGUID() const
{
	static FGuid PlayerPluginGUID(0xdd39a98d,
								  0x2e60507c,
								  0x0815829f,
								  0x7e2f891f);
	return PlayerPluginGUID;
}

bool FNDIMediaPlayer::Open(const FString& Url, const IMediaOptions* Options)
{
	Module = (FNDIMediaModule*)FModuleManager::Get().GetModule(
	"NDIMedia");
	NDIlib_find_instance_t pNDI_find = Module->GetNDIFind();

	CurrentState = EMediaState::Preparing;

	if (!pNDI_find)
	{
		return false;
	}

	if (!Super::Open(Url, Options))
	{
		return false;
	}

	FString Scheme;
	FString Location;

	// check scheme
	if (!Url.Split(TEXT("://"), &Scheme, &Location, ESearchCase::CaseSensitive))
	{
		return false;
	}

	FString SourceName = Location;

	if (Scheme == FString("ndimediain"))
	{
		uint32_t N = 0;
		const NDIlib_source_t* p_source = NDIlib_find_get_current_sources(pNDI_find, &N);

		int source_index = -1;

		for (uint32_t i = 0; i < N; i++)
		{
			FString ndi_name = FString(p_source[i].p_ndi_name);

			FRegexPattern pattern = FRegexPattern(TEXT("(.*) \\((.*)\\)"));
			FRegexMatcher matcher(pattern, ndi_name);

			if (!matcher.FindNext())
				continue;

			const FString machine_name = matcher.GetCaptureGroup(1);
			const FString ndi_source_name = matcher.GetCaptureGroup(2);

			if (SourceName == ndi_source_name)
			{
				source_index = i;
				break;
			}
		}

		if (source_index == -1)
		{
			return false;
		}

		NDIlib_recv_create_v3_t settings;
		settings.bandwidth = NDIlib_recv_bandwidth_highest;
		settings.allow_video_fields = false;
		// settings.color_format = NDIlib_recv_color_format_BGRX_BGRA;
		// settings.color_format = NDIlib_recv_color_format_UYVY_BGRA;
		settings.color_format = NDIlib_recv_color_format_best;
		
		pNDI_recv = NDIlib_recv_create_v3(&settings);
		NDIlib_recv_connect(pNDI_recv, p_source + source_index);

		CurrentState = EMediaState::Playing;
		EventSink.ReceiveMediaEvent(EMediaEvent::MediaConnecting);

		///
		
		// const UNDIVirtualMediaSource* Source = static_cast<const UNDIVirtualMediaSource*>(Options);
		// this->CineCameraActor = Source->Camera;
		// this->MetadataDelayFrames = Source->MetadataDelayFrames;
	}
	
	return true;
}

void FNDIMediaPlayer::Close()
{
	if (pNDI_recv)
	{
		NDIlib_recv_destroy(pNDI_recv);
		pNDI_recv = nullptr;
	}

	Super::Close();
}

void FNDIMediaPlayer::TickFetch(FTimespan DeltaTime, FTimespan Timecode)
{
	FMediaIOCorePlayerBase::TickFetch(DeltaTime, Timecode);

	if (!pNDI_recv)
		return;

	if (CurrentState != EMediaState::Playing)
		return;

	const int TimeoutInMS = 0;
	NDIlib_video_frame_v2_t* video_frame = new NDIlib_video_frame_v2_t();
	NDIlib_frame_type_e frame_type = NDIlib_recv_capture_v3(pNDI_recv, video_frame, nullptr, nullptr, TimeoutInMS);

	bool bVideoBufferUnderflowDetected = false;
	while (Samples->NumVideoSamples() > 1)
	{
		bVideoBufferUnderflowDetected = true;
		Samples->PopVideo();
	}

	if (bVideoBufferUnderflowDetected)
	{
		UE_LOG(LogTemp, Log, TEXT("NDI Video Buffer Underflow Detected!"));
	}

	if (frame_type == NDIlib_frame_type_video)
	{
		const FTimespan DecodedTime = FTimespan::FromSeconds(GetPlatformSeconds());
		const FFrameRate FrameRate(video_frame->frame_rate_N, video_frame->frame_rate_D);
		const TOptional<FTimecode> DecodedTimecode;

		// metadata
		if (video_frame->p_metadata)
		{
			const FString Metadata = FString(video_frame->p_metadata);

			const auto BinarySample = MetadataSamplePool->AcquireShared();

			BinarySample->Initialize((const uint8_t*)GetData(Metadata), GetNum(Metadata),
				DecodedTime, FrameRate, DecodedTimecode);

			Samples->AddMetadata(BinarySample);
		}

		// video
		if (video_frame->FourCC == NDIlib_FourCC_type_BGRA
			|| video_frame->FourCC == NDIlib_FourCC_type_BGRX)
		{
			const auto TextureSample = TextureSamplePool->AcquireShared();

			TextureSample->Initialize(
				video_frame->p_data,
				video_frame->line_stride_in_bytes * video_frame->yres,
				video_frame->line_stride_in_bytes,
				video_frame->xres,
				video_frame->yres,
				EMediaTextureSampleFormat::CharBGRA,
				DecodedTime,
				FrameRate,
				DecodedTimecode,
				true
			);

			Samples->AddVideo(TextureSample);
		}
		else if (video_frame->FourCC == NDIlib_FourCC_type_UYVY)
		{
			const auto TextureSample = TextureSamplePool->AcquireShared();
		
			TextureSample->Initialize(
				video_frame->p_data,
				video_frame->line_stride_in_bytes * video_frame->yres,
				video_frame->line_stride_in_bytes,
				video_frame->xres,
				video_frame->yres,
				EMediaTextureSampleFormat::CharUYVY,
				DecodedTime,
				FrameRate,
				DecodedTimecode,
				true
			);
		
			Samples->AddVideo(TextureSample);
		}
		else if (video_frame->FourCC == NDIlib_FourCC_type_P216
			|| video_frame->FourCC == NDIlib_FourCC_type_PA16)
		{
			const auto TextureSample = TextureSamplePool->AcquireShared();

			const int line_stride_in_bytes = video_frame->xres * sizeof(uint16_t) * 4;
			
			int n_pixels = video_frame->xres * video_frame->yres;
			std::vector<unsigned short> buffer(n_pixels * 4);

			const uint16_t* luminance = (const uint16_t*)video_frame->p_data;
			const uint16_t* cbcr = (const uint16_t*)video_frame->p_data + n_pixels;

			uint16_t* ptr;

			if (video_frame->FourCC == NDIlib_FourCC_type_P216)
			{
				ptr = &buffer[0];
				for (int i = 0; i < n_pixels; i++)
				{
					ptr[0] = 0xFFFF; // A
					ptr[1] = *luminance; // Y

					ptr += 4;
					luminance++;
				}
			}
			else if (video_frame->FourCC == NDIlib_FourCC_type_PA16)
			{
				const uint16_t* alpha = (const uint16_t*)video_frame->p_data + (int)(n_pixels * 2);

				ptr = &buffer[0];
				for (int i = 0; i < n_pixels; i++)
				{
					ptr[0] = *alpha; // A
					ptr[1] = *luminance; // Y

					ptr += 4;
					luminance++;
					alpha++;
				}
			}

			ptr = &buffer[0];
			for (int i = 0; i < n_pixels; i += 2)
			{
				ptr[2] = cbcr[0]; // Cb
				ptr[3] = cbcr[1]; // Cr
				ptr += 4;

				ptr[2] = cbcr[0]; // Cb
				ptr[3] = cbcr[1]; // Cr
				ptr += 4;

				cbcr += 2;
			}

			TextureSample->Initialize(
				&buffer[0],
				line_stride_in_bytes * video_frame->yres,
				line_stride_in_bytes,
				video_frame->xres,
				video_frame->yres,
				EMediaTextureSampleFormat::Y416,
				DecodedTime,
				FrameRate,
				DecodedTimecode,
				true
			);
			
			Samples->AddVideo(TextureSample);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("Unsupported pixel format"));
		}
	}

	NDIlib_recv_free_video_v2(pNDI_recv, video_frame);
	delete video_frame;
}

void FNDIMediaPlayer::TickInput(FTimespan DeltaTime, FTimespan Timecode)
{
	TickTimeManagement();
}

FString FNDIMediaPlayer::GetStats() const
{
	return FMediaIOCorePlayerBase::GetStats();
}

bool FNDIMediaPlayer::IsHardwareReady() const
{
	return true;
}

void FNDIMediaPlayer::SetupSampleChannels()
{
}

#if WITH_EDITOR
const FSlateBrush* FNDIMediaPlayer::GetDisplayIcon() const
{
	return nullptr;
}
#endif
