#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Winmm.lib")


#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")


// std
#include <iostream>
#include <string>

// Windows
#include <windows.h>
#include <wrl.h>
#include <wrl\implements.h>

// DirectX
#include <d3d11.h>

// Media Foundation
#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <Codecapi.h>
#include <unordered_map>

// Simplified error handling via macros
#ifdef DEBUG
#define ON_ERROR __debugbreak(); return 1;
#else
#define ON_ERROR system("pause"); return 1;
#endif

#define CHECK(x, err) if (!(x)) { std::cerr << err << std::endl; ON_ERROR; }
#define CHECK_HR(x, err) if (FAILED(x)) { std::cerr << err << std::endl; ON_ERROR; }

// Constants
const int frameRate = 60;
const int surfaceWidth = 1920;
const int surfaceHeight = 1080;
const int uncapKey = VK_F8;
const int quitKey = VK_F9;

////////////////////

#define CAPTURE_FILENAME L"sample.mp4"
IMFSinkWriter* mpWriter = NULL;
IMFMediaType* pVideoOutType = NULL;
DWORD writerVideoStreamIndex = 0;
DWORD totalSampleBufferSize = 0;


////////////////////
class RGBToNV12ConverterD3D11 {

private:
	ID3D11Device* pD3D11Device = NULL;
	ID3D11DeviceContext* pD3D11Context = NULL;
	ID3D11VideoDevice* pVideoDevice = NULL;
	ID3D11VideoContext* pVideoContext = NULL;
	ID3D11VideoProcessor* pVideoProcessor = NULL;
	ID3D11VideoProcessorInputView* pInputView = NULL;
	ID3D11Texture2D* pTexBgra = NULL;
	ID3D11VideoProcessorEnumerator* pVideoProcessorEnumerator = nullptr;
	std::unordered_map<ID3D11Texture2D*, ID3D11VideoProcessorOutputView*> outputViewMap1;

public:
	RGBToNV12ConverterD3D11(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, int nWidth, int nHeight)
		: pD3D11Device(pDevice), pD3D11Context(pContext)
	{
		pD3D11Device->AddRef();
		pD3D11Context->AddRef();

		pTexBgra = NULL;
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));
		desc.Width = nWidth;
		desc.Height = nHeight;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;

		HRESULT hr = pDevice->CreateTexture2D(&desc, NULL, &pTexBgra);

		hr = pDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&pVideoDevice);
		//CHECK_HR(hr);
		hr = pContext->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&pVideoContext);
		//CHECK_HR(hr);
		D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc =
		{
			D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
			{ 1, 1 }, desc.Width, desc.Height,
			{ 1, 1 }, desc.Width, desc.Height,
			D3D11_VIDEO_USAGE_PLAYBACK_NORMAL
		};
		hr = pVideoDevice->CreateVideoProcessorEnumerator(&contentDesc, &pVideoProcessorEnumerator);
		//CHECK_HR(hr);
		hr = pVideoDevice->CreateVideoProcessor(pVideoProcessorEnumerator, 0, &pVideoProcessor);
		D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = { 0, D3D11_VPIV_DIMENSION_TEXTURE2D, { 0, 0 } };
		hr = pVideoDevice->CreateVideoProcessorInputView(pTexBgra, pVideoProcessorEnumerator, &inputViewDesc, &pInputView);
	//	CHECK_HR(hr, "CreateVideoProcessorInputView");
	}

	~RGBToNV12ConverterD3D11()
	{
		for (auto& it : outputViewMap1)
		{
			ID3D11VideoProcessorOutputView* pOutputView3 = it.second;
			pOutputView3->Release();
		}

		pInputView->Release();
		pVideoProcessorEnumerator->Release();
		pVideoProcessor->Release();
		pVideoContext->Release();
		pVideoDevice->Release();
		pTexBgra->Release();
		pD3D11Context->Release();
		pD3D11Device->Release();
	}
	HRESULT ConvertRGBToNV12(ID3D11Texture2D* pRGBSrcTexture, ID3D11Texture2D* pDestTexture)
	{
		HRESULT hr;
		pD3D11Context->CopyResource(pTexBgra, pRGBSrcTexture);
		ID3D11VideoProcessorOutputView* pOutputView1 = nullptr;
		auto it = outputViewMap1.find(pDestTexture);
		if (it == outputViewMap1.end())
		{
			D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc = { D3D11_VPOV_DIMENSION_TEXTURE2D };
			hr = pVideoDevice->CreateVideoProcessorOutputView(pDestTexture, pVideoProcessorEnumerator, &outputViewDesc, &pOutputView1);
			outputViewMap1.insert({ pDestTexture, pOutputView1 });
		}
		else
		{
			pOutputView1 = it->second;
		}

		D3D11_VIDEO_PROCESSOR_STREAM stream = { true, 0, 0, 0, 0, NULL, pInputView, NULL };
		hr = pVideoContext->VideoProcessorBlt(pVideoProcessor, pOutputView1, 0, 1, &stream);
		CHECK_HR(hr, "VideoProcessorBlt");
		return hr;
	}


};

///////////////////////

int main()
{
	HRESULT hr;

	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> deviceManager;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> surface;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> BGRAsurface;

	Microsoft::WRL::ComPtr<IMFTransform> transform;
	Microsoft::WRL::ComPtr<IMFAttributes> attributes;
	Microsoft::WRL::ComPtr<IMFMediaEventGenerator> eventGen;
	DWORD inputStreamID;
	DWORD outputStreamID;

	long long ticksPerSecond;
	long long appStartTicks;
	long long encStartTicks;
	long long ticksPerFrame;

	
	std::unique_ptr<RGBToNV12ConverterD3D11> pConverter;



	// ------------------------------------------------------------------------
	// Initialize Media Foundation & COM
	// ------------------------------------------------------------------------

	hr = MFStartup(MF_VERSION);
	CHECK_HR(hr, "Failed to start Media Foundation");


	// ------------------------------------------------------------------------
	// Initialize D3D11
	// ------------------------------------------------------------------------

	// Driver types supported
	D3D_DRIVER_TYPE DriverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

	// Feature levels supported
	D3D_FEATURE_LEVEL FeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_1
	};
	UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

	D3D_FEATURE_LEVEL FeatureLevel;

	// Create device
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr,
			D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
			FeatureLevels, NumFeatureLevels, D3D11_SDK_VERSION, &device, &FeatureLevel, &context);
		if (SUCCEEDED(hr))
		{
			// Device creation success, no need to loop anymore
			break;
		}
	}

	CHECK_HR(hr, "Failed to create device");

	// Create device manager
	UINT resetToken;
	hr = MFCreateDXGIDeviceManager(&resetToken, &deviceManager);
	CHECK_HR(hr, "Failed to create DXGIDeviceManager");

	hr = deviceManager->ResetDevice(device.Get(), resetToken);
	CHECK_HR(hr, "Failed to assign D3D device to device manager");




	// ------------------------------------------------------------------------
	// Create surface
	// ------------------------------------------------------------------------

	D3D11_TEXTURE2D_DESC BGRAdesc = { 0 };
	BGRAdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	BGRAdesc.Width = surfaceWidth;
	BGRAdesc.Height = surfaceHeight;
	BGRAdesc.MipLevels = 1;
	BGRAdesc.ArraySize = 1;
	BGRAdesc.SampleDesc.Count = 1;
	BGRAdesc.Usage = D3D11_USAGE_DEFAULT;
	BGRAdesc.BindFlags = D3D11_BIND_RENDER_TARGET;
	BGRAdesc.CPUAccessFlags = 0;



	hr = device->CreateTexture2D(&BGRAdesc, NULL, &BGRAsurface);
	CHECK_HR(hr, "Could not create BGRA surface");

	pConverter.reset(new RGBToNV12ConverterD3D11(device.Get(), context.Get(), surfaceWidth, surfaceHeight));

	// ------------------------------------------------------------------------
	// Create surface
	// ------------------------------------------------------------------------

	D3D11_TEXTURE2D_DESC desc = { 0 };
	desc.Format = DXGI_FORMAT_NV12;
	desc.Width = surfaceWidth;
	desc.Height = surfaceHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET;
	desc.CPUAccessFlags = 0;


	hr = device->CreateTexture2D(&desc, NULL, &surface);
	CHECK_HR(hr, "Could not create surface");


	// ------------------------------------------------------------------------
	// Initialize hardware encoder MFT
	// ------------------------------------------------------------------------

	// Find encoder
	//Microsoft::WRL::ComPtr<IMFActivate*> activateRaw;

	IMFActivate** activateRaw;

	UINT32 activateCount = 0;




	MFT_REGISTER_TYPE_INFO videoNV12 = { MFMediaType_Video, MFVideoFormat_NV12 };
	MFT_REGISTER_TYPE_INFO videoH264 = { MFMediaType_Video, MFVideoFormat_H264 };

	UINT32 flags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER;

	hr = MFTEnumEx(
		MFT_CATEGORY_VIDEO_ENCODER,
		flags,
		&videoNV12,
		&videoH264,
		&activateRaw,
		&activateCount
	);

	// h264 output
	CHECK_HR(hr, "Failed to enumerate MFTs");

	CHECK(activateCount, "No MFTs found");

	// Choose the first available encoder
	Microsoft::WRL::ComPtr<IMFActivate> activate = activateRaw[0];

	for (UINT32 i = 0; i < activateCount; i++)
		activateRaw[i]->Release();

	// Activate
	hr = activate->ActivateObject(IID_PPV_ARGS(&transform));
	CHECK_HR(hr, "Failed to activate MFT");

	// Get attributes
	hr = transform->GetAttributes(&attributes);
	CHECK_HR(hr, "Failed to get MFT attributes");

	// Get encoder name
	UINT32 nameLength = 0;
	std::wstring name;

	hr = attributes->GetStringLength(MFT_FRIENDLY_NAME_Attribute, &nameLength);
	CHECK_HR(hr, "Failed to get MFT name length");

	// IMFAttributes::GetString returns a null-terminated wide string
	name.resize(nameLength + 1);

	hr = attributes->GetString(MFT_FRIENDLY_NAME_Attribute, &name[0], name.size(), &nameLength);
	CHECK_HR(hr, "Failed to get MFT name");

	name.resize(nameLength);

	std::wcout << name << std::endl;

	// Unlock the transform for async use and get event generator
	hr = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
	CHECK_HR(hr, "Failed to unlock MFT");

	//eventGen = transform;

	transform.As(&eventGen);

	CHECK(eventGen, "Failed to QI for event generator");

	// Get stream IDs (expect 1 input and 1 output stream)
	hr = transform->GetStreamIDs(1, &inputStreamID, 1, &outputStreamID);
	if (hr == E_NOTIMPL)
	{
		inputStreamID = 0;
		outputStreamID = 0;
		hr = S_OK;
	}
	CHECK_HR(hr, "Failed to get stream IDs");


	// ------------------------------------------------------------------------
	// Configure hardware encoder MFT
	// ------------------------------------------------------------------------

	// Set D3D manager
	hr = transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(deviceManager.Get()));
	CHECK_HR(hr, "Failed to set D3D manager");

	// Set low latency hint
	hr = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
	CHECK_HR(hr, "Failed to set MF_LOW_LATENCY");

	// Set output type
	Microsoft::WRL::ComPtr<IMFMediaType> outputType;

	hr = MFCreateMediaType(&outputType);
	CHECK_HR(hr, "Failed to create media type");

	hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	CHECK_HR(hr, "Failed to set MF_MT_MAJOR_TYPE on H264 output media type");

	hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	CHECK_HR(hr, "Failed to set MF_MT_SUBTYPE on H264 output media type");

	hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, 30000000);
	CHECK_HR(hr, "Failed to set average bit rate on H264 output media type");

	hr = MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, desc.Width, desc.Height);
	CHECK_HR(hr, "Failed to set frame size on H264 MFT out type");

	hr = MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, 60, 1);
	CHECK_HR(hr, "Failed to set frame rate on H264 MFT out type");

	hr = outputType->SetUINT32(MF_MT_INTERLACE_MODE, 2);
	CHECK_HR(hr, "Failed to set MF_MT_INTERLACE_MODE on H.264 encoder MFT");

	hr = outputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	CHECK_HR(hr, "Failed to set MF_MT_ALL_SAMPLES_INDEPENDENT on H.264 encoder MFT");

	hr = transform->SetOutputType(outputStreamID, outputType.Get(), 0);
	CHECK_HR(hr, "Failed to set output media type on H.264 encoder MFT");

	// Set input type
	Microsoft::WRL::ComPtr<IMFMediaType> inputType;

	hr = MFCreateMediaType(&inputType);
	CHECK_HR(hr, "Failed to create media type");

	for (DWORD i = 0;; i++)
	{
		inputType = nullptr;
		hr = transform->GetInputAvailableType(inputStreamID, i, &inputType);
		CHECK_HR(hr, "Failed to get input type");

		hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		CHECK_HR(hr, "Failed to set MF_MT_MAJOR_TYPE on H264 MFT input type");

		hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		CHECK_HR(hr, "Failed to set MF_MT_SUBTYPE on H264 MFT input type");

		hr = MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, desc.Width, desc.Height);
		CHECK_HR(hr, "Failed to set MF_MT_FRAME_SIZE on H264 MFT input type");

		hr = MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, 60, 1);
		CHECK_HR(hr, "Failed to set MF_MT_FRAME_RATE on H264 MFT input type");

		hr = transform->SetInputType(inputStreamID, inputType.Get(), 0);
		CHECK_HR(hr, "Failed to set input type");

		break;
	}


	// ------------------------------------------------------------------------
	// Start encoding
	// ------------------------------------------------------------------------

	// Initialize timer
	timeBeginPeriod(1);

	LARGE_INTEGER ticksInt;
	long long ticks;
	QueryPerformanceFrequency(&ticksInt);
	ticksPerSecond = ticksInt.QuadPart;

	QueryPerformanceCounter(&ticksInt);
	appStartTicks = ticksInt.QuadPart;

	ticksPerFrame = ticksPerSecond / frameRate;

	// Start encoder
	hr = transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
	CHECK_HR(hr, "Failed to process FLUSH command on H.264 MFT");

	hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL);
	CHECK_HR(hr, "Failed to process BEGIN_STREAMING command on H.264 MFT");

	hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL);
	CHECK_HR(hr, "Failed to process START_OF_STREAM command on H.264 MFT");



	//////////////////////////////////////////////////////
	/*

	CHECK_HR(MFCreateSinkWriterFromURL(
		CAPTURE_FILENAME,
		NULL,
		NULL,
		&pWriter),
		"Error creating mp4 sink writer.");

	// Configure the output video type on the sink writer.
	CHECK_HR(MFCreateMediaType(&pVideoOutType), "Configure encoder failed to create media type for video output sink.");

	CHECK_HR(outputType->CopyAllItems(pVideoOutType),
		"Failed to copy all media types from MFT output to sink input.");

	CHECK_HR(pWriter->AddStream(pVideoOutType, &writerVideoStreamIndex),
		"Failed to add the video stream to the sink writer.");

	pVideoOutType->Release();

	// Ready to go.

	CHECK_HR(pWriter->BeginWriting(),
		"Sink writer begin writing call failed.");
	*/











	///////////////////////////////////////////////////////////

	// Main encode loop
	// Assume that METransformNeedInput and METransformHaveOutput are sent in a regular alternating pattern.
	// Otherwise a queue is needed for performance measurement.
	bool encoding = false;
	bool throttle = false;

	//while (!(GetAsyncKeyState(quitKey) & (1 << 15)))
	for( int x =0 ; x < 1000 ; ++x)
	{
		// Get next event
		Microsoft::WRL::ComPtr<IMFMediaEvent> event;
		hr = eventGen->GetEvent(0, &event);
		CHECK_HR(hr, "Failed to get next event");

		MediaEventType eventType;
		hr = event->GetType(&eventType);
		CHECK_HR(hr, "Failed to get event type");

		switch (eventType)
		{
		case METransformNeedInput:
			CHECK(!encoding, "Expected METransformHaveOutput");
			encoding = true;

			{
				throttle = !(GetAsyncKeyState(uncapKey) & (1 << 15));

				if (throttle)
				{
					// Calculate next frame time by quantizing time to the next value divisble by ticksPerFrame
					QueryPerformanceCounter(&ticksInt);
					ticks = ticksInt.QuadPart;

					long long nextFrameTicks = (ticks / ticksPerFrame + 1) * ticksPerFrame;

					// Wait for next frame
					while (ticks < nextFrameTicks)
					{
						// Not accurate, but enough for this purpose
						Sleep(1);

						QueryPerformanceCounter(&ticksInt);
						ticks = ticksInt.QuadPart;
					}
				}


				hr = pConverter->ConvertRGBToNV12(BGRAsurface.Get(), surface.Get());
				if (hr != S_OK)
				{
					int x = 1;
				}
				CHECK_HR(hr,"ConvertRGBToNV12");

				// Create buffer
				Microsoft::WRL::ComPtr<IMFMediaBuffer> inputBuffer;
				hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), surface.Get(), 0, FALSE, &inputBuffer);
				CHECK_HR(hr, "Failed to create IMFMediaBuffer");

				// Create sample
				Microsoft::WRL::ComPtr<IMFSample> sample;
				hr = MFCreateSample(&sample);
				CHECK_HR(hr, "Failed to create IMFSample");
				hr = sample->AddBuffer(inputBuffer.Get());
				CHECK_HR(hr, "Failed to add buffer to IMFSample");

				// Start measuring encode time
				QueryPerformanceCounter(&ticksInt);
				encStartTicks = ticksInt.QuadPart;

				hr = transform->ProcessInput(inputStreamID, sample.Get(), 0);
				CHECK_HR(hr, "Failed to process input");
			}

			break;

		case METransformHaveOutput:
			CHECK(encoding, "Expected METransformNeedInput");
			encoding = false;

			{
				DWORD status;
				MFT_OUTPUT_DATA_BUFFER outputBuffer;
				outputBuffer.dwStreamID = outputStreamID;
				outputBuffer.pSample = nullptr;
				outputBuffer.dwStatus = 0;
				outputBuffer.pEvents = nullptr;

				hr = transform->ProcessOutput(0, 1, &outputBuffer, &status);
				CHECK_HR(hr, "ProcessOutput failed");

				// Stop measuring encode time
				QueryPerformanceCounter(&ticksInt);
				ticks = ticksInt.QuadPart;

				long double encTime_ms = (ticks - encStartTicks) * 1000 / (long double)ticksPerSecond;
				long double appTime_s = (encStartTicks - appStartTicks) / (long double)ticksPerSecond;

				////////////////////////////////////////////////////////
				
				if (mpWriter == NULL)
				{
					IMFMediaType* pType;
					hr = transform->GetOutputAvailableType(0, 0, &pType);

					IMFByteStream* pByteStream;
					IMFMediaSink* pMediaSink;
					hr = MFCreateFile(MF_ACCESSMODE_READWRITE, MF_OPENMODE_DELETE_IF_EXIST, MF_FILEFLAGS_NONE, CAPTURE_FILENAME, &pByteStream);
					hr = MFCreateMPEG4MediaSink(pByteStream, pType, NULL, &pMediaSink);
					hr = MFCreateSinkWriterFromMediaSink(pMediaSink, NULL, &mpWriter);
					hr = mpWriter->BeginWriting();
				}
				hr = mpWriter->WriteSample(0, outputBuffer.pSample);
				






				////////////////////////////////////////////////////////////



				// Report data
				std::cout << appTime_s << " " << encTime_ms << " " << throttle << std::endl;

				// Release sample as it is not processed any further.
				if (outputBuffer.pSample)
					outputBuffer.pSample->Release();
				if (outputBuffer.pEvents)
					outputBuffer.pEvents->Release();

				CHECK_HR(hr, "Failed to process output");
			}
			break;

		default:
			CHECK(false, "Unknown event");
			break;
		}
	}

	hr = mpWriter->Finalize();
	// ------------------------------------------------------------------------
	// Finish encoding
	// ------------------------------------------------------------------------

	hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL);
	CHECK_HR(hr, "Failed to process END_OF_STREAM command on H.264 MFT");

	hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL);
	CHECK_HR(hr, "Failed to process END_STREAMING command on H.264 MFT");

	hr = transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
	CHECK_HR(hr, "Failed to process FLUSH command on H.264 MFT");

	return 0;
}

