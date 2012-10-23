// Easy Web Camera LIBrary "ewclib.h"  by I.N.
// OS:WindowsXP
// Compiler:Visual C++ .NET 2003 (+DirectX 9.0 SDK)

// 2005/03/26 ver.0.1
// 2005/03/28 ver.0.2 add retry routine
// 2005/03/29 ver.1.0
// 2005/04/01 ver.1.x add skip mode, max=8
// 2005/04/04 ver.1.1 remove skip mode, but check displayname
// 2005/04/05 ver.1.2 debug. use WideCharToMultiByte()

#ifndef EWCLIB_H
#define EWCLIB_H

#include <dshow.h>
#include <qedit.h>
#include <wchar.h>
#include <math.h>

#pragma comment(lib,"strmiids.lib")

#ifndef EWC_NCAMMAX
#define EWC_NCAMMAX 8
#endif
int ewc_init=0;
int ewc_ncam;
int ewc_wx[EWC_NCAMMAX];
int ewc_wy[EWC_NCAMMAX];
int *ewc_buffer[EWC_NCAMMAX];
long ewc_bufsize[EWC_NCAMMAX];

IGraphBuilder *ewc_pGraph;
IBaseFilter *ewc_pF[EWC_NCAMMAX];
ISampleGrabber *ewc_pGrab[EWC_NCAMMAX];
ICaptureGraphBuilder2 *ewc_pBuilder[EWC_NCAMMAX];
IBaseFilter *ewc_pCap[EWC_NCAMMAX];

int numCheck(int num)
{
	if(!ewc_init) return 1;
	if(num<0 || num>=ewc_ncam) return 2;
	
	return 0;
}

//カメラ台数を返す
int EWC_GetCamera(void)
{
	if(!ewc_init) return 0;
	return ewc_ncam;
}

//カメラ(番号:num)のフレームバッファサイズ(単位:バイト)を返す
int EWC_GetBufferSize(int num)
{
	if(numCheck(num)) return 0;
	return ewc_bufsize[num];
}

//フィルタのピンを取得する
IPin *ewc_GetPin(IBaseFilter *pFilter, PIN_DIRECTION PinDir)
{
	IEnumPins *pEnum;
	IPin *pPin=0;
	HRESULT hr;

	hr= pFilter->EnumPins(&pEnum);
	if(hr!=S_OK) return NULL;

	while(pEnum->Next(1,&pPin,0)==S_OK){
		PIN_DIRECTION PinDirThis;
		pPin->QueryDirection(&PinDirThis);
		if(PinDir==PinDirThis) break;
		pPin->Release();
	}
	pEnum->Release();
	return pPin;
}

int EWC_Open(GUID type, int wx, int wy, double fps)
{
	IAMStreamConfig *ewc_pConfig;
	IMediaControl *ewc_pMediaControl;
	HRESULT hr;
	int i,errcode;
	int retryflag,t0,t;

	if(ewc_init){errcode=1; goto fin;}

	retryflag=0;

cont:
	//各変数の初期化
	errcode=0;
	ewc_pGraph=0;
	ewc_pMediaControl=0;
	ewc_pConfig=0;
	for(i=0;i<EWC_NCAMMAX;i++){
		ewc_pGrab[i]=0;
		ewc_pF[i]=0;
		ewc_pCap[i]=0;
		ewc_pBuilder[i]=0;
		ewc_buffer[i]=0;
	}

	//COM初期化
	hr= CoInitialize(0);
	if(hr!=S_OK){errcode=2; goto fin;}

	//フィルタグラフマネージャ作成
	hr= CoCreateInstance(CLSID_FilterGraph,0,CLSCTX_INPROC_SERVER,IID_IGraphBuilder,(void **)&ewc_pGraph);
	if(hr!=S_OK){errcode=3; goto fin;}

	//システムデバイス列挙子の作成
	ICreateDevEnum *pDevEnum=0;
	hr= CoCreateInstance(CLSID_SystemDeviceEnum,0,CLSCTX_INPROC_SERVER,IID_ICreateDevEnum,(void **)&pDevEnum);
	if(hr!=S_OK){errcode=4; goto fin;}
	//列挙子の取得
	IEnumMoniker *pEnum=0;
	hr= pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,&pEnum,0);
	if(hr!=S_OK){
		//ESP_Printf("No driver\n");
		errcode=5; goto fin;
	}

	//モニカの取得
	ULONG cFetched;
	IMoniker *pMoniker=0;
	wchar_t SrcName[32];
	ewc_ncam=0;
	char displayname[512];
	for(i=0;i<EWC_NCAMMAX;i++){
		if(pEnum->Next(1,&pMoniker,&cFetched)==S_OK){
			//DisplayNameの取得
			LPOLESTR strMonikerName=0;
			hr = pMoniker->GetDisplayName(NULL,NULL,&strMonikerName);
			if(hr!=S_OK){errcode=6; goto fin;}
			WideCharToMultiByte(CP_ACP,0,strMonikerName,-1,displayname,sizeof(displayname),0,0);
			//ESP_Printf("displayname(%d):%s\n",i,displayname);
			
			//DisplayNameに'@device:pnp'があれば登録
			if(strstr(displayname,"@device:pnp")){
				//オブジェクト初期化
				pMoniker->BindToObject(0,0,IID_IBaseFilter,(void **)&ewc_pCap[ewc_ncam]);
				pMoniker->Release();
				pMoniker=0;

				//グラフにフィルタを追加
				swprintf(SrcName,L"Video Capture %d",ewc_ncam);
				hr= ewc_pGraph->AddFilter(ewc_pCap[ewc_ncam], SrcName);
				if(hr!=S_OK){errcode=7; goto fin;}
				ewc_ncam++;
			}
		}
	}
	pEnum->Release();
	pEnum=0;
	pDevEnum->Release();
	pDevEnum=0;

	//No camera
	if(!ewc_ncam){errcode=8; goto fin;}

	//ESP_Printf("camera=%d\n",ewc_ncam);

	//キャプチャビルダの作成
	for(i=0;i<ewc_ncam;i++){
		ewc_wx[i]=wx;
		ewc_wy[i]=wy;

		CoCreateInstance(CLSID_CaptureGraphBuilder2,0,CLSCTX_INPROC_SERVER,
			IID_ICaptureGraphBuilder2,(void **)&ewc_pBuilder[i]);
		hr= ewc_pBuilder[i]->SetFiltergraph(ewc_pGraph);
		if(hr!=S_OK){errcode=9; goto fin;}

		//IAMStreamConfigインタフェースの取得
		hr= ewc_pBuilder[i]->FindInterface(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,
			ewc_pCap[i],IID_IAMStreamConfig,(void**)&ewc_pConfig);
		if(hr!=S_OK){errcode=10; goto fin;}

		//画像サイズ，フレームレートの設定
		AM_MEDIA_TYPE *ewc_pmt[EWC_NCAMMAX];
		hr= ewc_pConfig->GetFormat(&ewc_pmt[i]);
		VIDEOINFOHEADER *vh = (VIDEOINFOHEADER*)ewc_pmt[i]->pbFormat;
		vh->bmiHeader.biWidth =ewc_wx[i];
		vh->bmiHeader.biHeight=ewc_wy[i]; 
		vh->AvgTimePerFrame= (REFERENCE_TIME)floor((10000000.0/fps+0.5));
		hr=ewc_pConfig->SetFormat(ewc_pmt[i]);
		if(hr!=S_OK){errcode=11; goto fin;}
		ewc_pConfig->Release();
		ewc_pConfig=0;

		//サンプルグラバの生成 ewc_pF[]
		CoCreateInstance(CLSID_SampleGrabber,0,CLSCTX_INPROC_SERVER,IID_IBaseFilter,(LPVOID *)&ewc_pF[i]);
		hr= ewc_pF[i]->QueryInterface(IID_ISampleGrabber,(void **)&ewc_pGrab[i]);
		if(hr!=S_OK){errcode=12; goto fin;}

		//メディアタイプの設定
		AM_MEDIA_TYPE ewc_mt[EWC_NCAMMAX];
		ZeroMemory(&ewc_mt[i],sizeof(AM_MEDIA_TYPE));
		ewc_mt[i].majortype=MEDIATYPE_Video;
		//Qcam Pro 4000では次の値が設定可能だった．
		//MEDIASUBTYPE_RGB4
		//MEDIASUBTYPE_RGB8
		//MEDIASUBTYPE_RGB565
		//MEDIASUBTYPE_RGB555
		//MEDIASUBTYPE_RGB24
		//MEDIASUBTYPE_RGB32
		//MEDIASUBTYPE_ARGB32
		ewc_mt[i].subtype=type;
		ewc_mt[i].formattype=FORMAT_VideoInfo;
		hr = ewc_pGrab[i]->SetMediaType(&ewc_mt[i]);
		if(hr!=S_OK){errcode=13; goto fin;}
		//フィルタグラフへの追加
		wchar_t GrabName[32];
		swprintf(GrabName,L"Grabber %d",i);
		hr= ewc_pGraph->AddFilter(ewc_pF[i], GrabName);
		if(hr!=S_OK){errcode=14; goto fin;}

		//サンプルグラバの接続
		IPin *ewc_pSrcOut[EWC_NCAMMAX];
		IPin *ewc_pSGrabIn[EWC_NCAMMAX];
		// ピンの取得
		ewc_pSrcOut[i]=ewc_GetPin(ewc_pCap[i],PINDIR_OUTPUT);
		ewc_pSGrabIn[i]=ewc_GetPin(ewc_pF[i],PINDIR_INPUT);

		// ピンの接続
		hr = ewc_pGraph->Connect(ewc_pSrcOut[i], ewc_pSGrabIn[i]);
		if(hr!=S_OK){
			errcode=15;
			goto fin;
		}
		//ESP_Printf("Connected(%p,%p)\n",ewc_pSrcOut[i],ewc_pSGrabIn[i]);
		ewc_pSrcOut[i]->Release();
		ewc_pSrcOut[i]=0;
		ewc_pSGrabIn[i]->Release();
		ewc_pSGrabIn[i]=0;

		//グラバのモード設定
		hr = ewc_pGrab[i]->SetBufferSamples(TRUE);
		if(hr!=S_OK){errcode=16; goto fin;}
		hr = ewc_pGrab[i]->SetOneShot(FALSE);
		if(hr!=S_OK){errcode=17; goto fin;}
	}

	//キャプチャ開始
	hr= ewc_pGraph->QueryInterface(IID_IMediaControl,(void **)&ewc_pMediaControl);
	if(hr!=S_OK){errcode=18; goto fin;}
	hr= ewc_pMediaControl->Run();
	if(hr!=S_OK){errcode=19; goto fin;}
	ewc_pMediaControl->Release();
	ewc_pMediaControl=0;

	//バッファサイズの取得
	for(i=0;i<ewc_ncam;i++){
		ewc_bufsize[i]=0;
		t0=GetTickCount();
		do{
			ewc_pGrab[i]->GetCurrentBuffer(&ewc_bufsize[i],0);
			t=GetTickCount();
			if((t-t0)>3000){errcode=20; retryflag++; goto fin;}
		}while(ewc_bufsize[i]<=0);
		if(ewc_bufsize[i]) ewc_buffer[i]=(int *)malloc(ewc_bufsize[i]);
		if(!ewc_bufsize[i] || !ewc_buffer[i]){errcode=20; goto fin;}
	}

fin:
	if(errcode){
		if(!ewc_init){

		if(pDevEnum) pDevEnum->Release();
		if(pEnum) pEnum->Release();
		if(pMoniker) pMoniker->Release();
		if(ewc_pMediaControl) ewc_pMediaControl->Release();
		if(ewc_pConfig) ewc_pConfig->Release();

		for(i=0;i<EWC_NCAMMAX;i++){
			if(ewc_buffer[i]) free(ewc_buffer[i]);
			if(ewc_pGrab[i]) ewc_pGrab[i]->Release();
			if(ewc_pF[i]) ewc_pF[i]->Release();
			if(ewc_pCap[i]) ewc_pCap[i]->Release();
			if(ewc_pBuilder[i]) ewc_pBuilder[i]->Release();
		}
		if(ewc_pGraph) ewc_pGraph->Release();
		CoUninitialize();
		if(retryflag){
			//ESP_Printf("Retry\n"); 
			//正常に接続されるまでリトライ（３回まで）
			if(retryflag<=2) goto cont;
		}
		}
	}else{
		ewc_init=1;
	}
	return errcode;
}

//カメラ(番号:num)の画像取得
int EWC_GetImage(int num, void *buffer)
{
	int y,wx,wy,byte;
	HRESULT hr;

	if(numCheck(num)) return 1;

	wx=ewc_wx[num];
	wy=ewc_wy[num];
	byte=ewc_bufsize[num]/ewc_wy[num];

	hr= ewc_pGrab[num]->GetCurrentBuffer(&ewc_bufsize[num],(long *)ewc_buffer[num]);
	if(hr!=S_OK) return 2;

	//画像の上下を逆にしてコピー
	for(y=0;y<wy;y++){
		memcpy((unsigned char *)buffer+(wy-1-y)*byte, (unsigned char *)ewc_buffer[num]+y*byte,byte);
	}
	return 0;
}

//終了処理
int EWC_Close(void)
{
	IMediaControl *ewc_pMediaControl;
	HRESULT hr;
	int i;

	if(!ewc_init) return 1;

	//IMediaControlインターフェイス取得
	hr= ewc_pGraph->QueryInterface(IID_IMediaControl,(void **)&ewc_pMediaControl);
	if(hr!=S_OK) return 2;
	ewc_pMediaControl->Stop();
	ewc_pMediaControl->Release();

	//メモリ解放
	for(i=0;i<ewc_ncam;i++){
		ewc_pGrab[i]->Release();
		ewc_pF[i]->Release();
		ewc_pCap[i]->Release();
		ewc_pBuilder[i]->Release();
		if(ewc_buffer[i]) free(ewc_buffer[i]);
	}
	ewc_pGraph->Release();

	CoUninitialize();
	ewc_init=0;

	return 0;
}

#endif
