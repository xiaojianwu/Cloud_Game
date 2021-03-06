#include "SoundEncoder.h"
SoundEncoder::SoundEncoder()
{
	this->device=0;
	this->enumerator=0;
	this->audioClient=0;
	this->waveFormat=0;
	this->audioCaptureClient=0;
	runFlag=false;
}
SoundEncoder::~SoundEncoder()
{
	
}
void SoundEncoder::startFrameLoop()
{
	runFlag=true;
	HRESULT hr;
	long frameCounter=0;
	while(runFlag)
	{
		frameCounter++;
		UINT32 nextPacketSize;
		hr = audioCaptureClient->GetNextPacketSize(&nextPacketSize);
		if (FAILED(hr)) 
		{
			  printf("IAudioCaptureClient::GetNextPacketSize failed on pass %u: hr = 0x%08x\n", frameCounter, hr);
			  audioClient->Stop();
			  audioCaptureClient->Release();
			  audioClient->Release(); 
			  runFlag=false;
			  return;
		}

		if (nextPacketSize == 0) 
		{ // no data yet
		  continue;
		}

    // get the captured data
		BYTE *data;
		UINT32 frameCount;
		DWORD bufferFlags;

		hr = audioCaptureClient->GetBuffer(&data, &frameCount, &bufferFlags, NULL, NULL);
		if (FAILED(hr)) 
		{
		  printf("IAudioCaptureClient::GetBuffer failed on pass %u: hr = 0x%08x\n", frameCounter, hr);
		  audioClient->Stop();
		  audioCaptureClient->Release();
		  audioClient->Release();   
		  runFlag=false;
		  return;            
		}

		if (bufferFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
		  printf("IAudioCaptureClient::GetBuffer reports 'data discontinuity' on pass %u\n", frameCounter);
		}
		if (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
		  printf("IAudioCaptureClient::GetBuffer reports 'silent' on pass %u\n", frameCounter);
		}
		if (bufferFlags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) {
		  printf("IAudioCaptureClient::GetBuffer reports 'timestamp error' on pass %u\n", frameCounter);
		}

		if (frameCount == 0) {
		  printf("IAudioCaptureClient::GetBuffer said to read 0 frames on pass %u\n", frameCounter);
		  audioClient->Stop();
		  audioCaptureClient->Release();
		  audioClient->Release();
		  runFlag=false;
		  return;            
		}

		LONG bytesToWrite = frameCount * waveFormat->nBlockAlign;
		printf("Recording\n");
		//===========Encode data and byteToWrite===========
		//======================
		  hr = audioCaptureClient->ReleaseBuffer(frameCount);
		if (FAILED(hr))	
		{
		  printf("IAudioCaptureClient::ReleaseBuffer failed on pass %u: hr = 0x%08x\n", frameCounter, hr);
		  audioClient->Stop();
		  audioCaptureClient->Release();
		  audioClient->Release();  
		  runFlag=false;
		  return;            
		}
	}
	audioClient->Stop();
	audioCaptureClient->Release();
	audioClient->Release();
}
void SoundEncoder::stopFrameLoop()
{
	runFlag=false;
}
bool SoundEncoder::initSoundEncoder()
{
	return true;
}
bool SoundEncoder::initSoundCapture()
{
	CoInitialize(0);
	CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), 
    (void**) &enumerator);
	enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	HRESULT hr;
	hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**) &audioClient); 
	if (FAILED(hr)) {
		printf("IMMDevice::Activate(IAudioClient) failed: hr = 0x%08x", hr);
		return false;
	}
	
	hr = audioClient->GetMixFormat(&waveFormat);
	if (FAILED(hr)) 
	{
		printf("IAudioClient::GetMixFormat failed: hr = 0x%08x\n", hr);
		CoTaskMemFree(waveFormat);
		audioClient->Release();
		return false;
	}
	switch (waveFormat->wFormatTag) 
	{
	  case WAVE_FORMAT_IEEE_FLOAT:
		waveFormat->wFormatTag = WAVE_FORMAT_PCM;
		waveFormat->wBitsPerSample = 16;
		waveFormat->nBlockAlign = waveFormat->nChannels * waveFormat->wBitsPerSample / 8;
		waveFormat->nAvgBytesPerSec = waveFormat->nBlockAlign * waveFormat->nSamplesPerSec;
		break;
		case WAVE_FORMAT_EXTENSIBLE:
		{
		  // naked scope for case-local variable
		  PWAVEFORMATEXTENSIBLE waveFormatEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(waveFormat);
		  if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, waveFormatEx->SubFormat)) 
		  {
			waveFormatEx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
			waveFormatEx->Samples.wValidBitsPerSample = 16;
			waveFormat->wBitsPerSample = 16;
			waveFormat->nBlockAlign = waveFormat->nChannels * waveFormat->wBitsPerSample / 8;
			waveFormat->nAvgBytesPerSec = waveFormat->nBlockAlign * waveFormat->nSamplesPerSec;
		  } else 
		  {
			printf("Don't know how to coerce mix format to int-16\n");
			CoTaskMemFree(waveFormat);
			audioClient->Release();
			return false;
		  }
		}
		break;
	  default:
		printf("Don't know how to coerce WAVEFORMATEX with wFormatTag = 0x%08x to int-16\n", waveFormat->wFormatTag);
		CoTaskMemFree(waveFormat);
		audioClient->Release();
		return false;
	}
	

	  // call IAudioClient::Initialize
	  // note that AUDCLNT_STREAMFLAGS_LOOPBACK and AUDCLNT_STREAMFLAGS_EVENTCALLBACK do not work together...
	  // the "data ready" event never gets set, so we're going to do a timer-driven loop
	  hr = audioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_LOOPBACK,
		10000000, 0, waveFormat, 0
		);
	  if (FAILED(hr)) {
		printf("IAudioClient::Initialize failed: hr = 0x%08x\n", hr);
		audioClient->Release();
		return false;
	  }
	  CoTaskMemFree(waveFormat);

	  // activate an IAudioCaptureClient
	  
	  hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**) &audioCaptureClient);
	  if (FAILED(hr)) {
		printf("IAudioClient::GetService(IAudioCaptureClient) failed: hr 0x%08x\n", hr);
		audioClient->Release();
		return false;
	  }

	  hr = audioClient->Start();
	  if (FAILED(hr)) {
		printf("IAudioClient::Start failed: hr = 0x%08x\n", hr);
		audioCaptureClient->Release();
		audioClient->Release();
		return false;
	  }
	return true;


}