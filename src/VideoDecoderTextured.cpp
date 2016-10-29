#include "VideoDecoderTextured.h"


VideoDecoderTextured::VideoDecoderTextured()
{
}





OMX_ERRORTYPE VideoDecoderTextured::onFillBuffer(Component* component, OMX_BUFFERHEADERTYPE* pBuffer)
{	
	OMX_ERRORTYPE error = OMX_FillThisBuffer(component->handle, pBuffer);
	return error;
}


bool VideoDecoderTextured::open(StreamInfo streamInfo_,
                                OMXClock* omxClock_,
                                ofxOMXPlayerSettings& settings_,
                                EGLImageKHR eglImage)
{
	OMX_ERRORTYPE error   = OMX_ErrorNone;

    settings = settings_;
    streamInfo = streamInfo_;
    doFilters = settings.enableFilters;
    OMX_Maps::getInstance().filtersEnabled = doFilters;
    omxClock = omxClock_;
    clockComponent = omxClock->getComponent();
    
	videoWidth  = streamInfo.width;
	videoHeight = streamInfo.height;

	if(!videoWidth || !videoHeight)
	{
		return false;
	}

	if(streamInfo.extrasize > 0 && streamInfo.extradata != NULL)
	{
		extraSize = streamInfo.extrasize;
		extraData = (uint8_t *)malloc(extraSize);
		memcpy(extraData, streamInfo.extradata, streamInfo.extrasize);
	}

	processCodec(streamInfo);


	std::string componentName = "OMX.broadcom.video_decode";
    decoderComponent = new Component();
	if(!decoderComponent->init(componentName, OMX_IndexParamVideoInit))
	{
		return false;
	}
    
	componentName = "OMX.broadcom.egl_render";
    renderComponent = new Component();

	if(!renderComponent->init(componentName, OMX_IndexParamVideoInit))
	{
		return false;
	}

	componentName = "OMX.broadcom.video_scheduler";
    schedulerComponent = new Component();

	if(!schedulerComponent->init(componentName, OMX_IndexParamVideoInit))
	{
		return false;
	}
    
    if(doFilters)
    {
        componentName = "OMX.broadcom.image_fx";
        
        imageFXComponent = new Component();

        
        if(!imageFXComponent->init(componentName, OMX_IndexParamImageInit))
        {
            return false;
        }
        
        OMX_PARAM_U32TYPE extra_buffers;
        OMX_INIT_STRUCTURE(extra_buffers);
        extra_buffers.nU32 = (OMX_U32)5;
        
        error = decoderComponent->setParameter(OMX_IndexParamBrcmExtraBuffers, &extra_buffers);
        OMX_TRACE(error);

        
        OMX_PARAM_PORTDEFINITIONTYPE portFormat;
        OMX_INIT_STRUCTURE(portFormat);
        
        portFormat.nPortIndex = imageFXComponent->outputPort;
        error = imageFXComponent->getParameter(OMX_IndexParamPortDefinition, &portFormat);
        OMX_TRACE(error);
        ofLogVerbose() << "nBufferCountActual: " << portFormat.nBufferCountActual;
        ofLogVerbose() << "nBufferCountMin: " << portFormat.nBufferCountMin;
        
        ofLogVerbose() << "nBufferSize: " << portFormat.nBufferSize;
        switch (portFormat.eDomain) 
        {
            case OMX_PortDomainAudio:
            {
                ofLogVerbose() << "audio";
                break;
            }
            case OMX_PortDomainVideo:
            {
                ofLogVerbose() << "video";
                break;
            }
            case OMX_PortDomainImage:
            {
                ofLogVerbose() << "image";
                break;
            }
            case OMX_PortDomainOther:
            {
                ofLogVerbose() << "other";
                break;
            }                
        }
        
        //portFormat.eDomain = OMX_PortDomainVideo;
        error = imageFXComponent->setParameter(OMX_IndexParamPortDefinition, &portFormat);
        OMX_TRACE(error);
        decoderTunnel = new Tunnel();
        decoderTunnel->init(decoderComponent, 
                           decoderComponent->outputPort, 
                           imageFXComponent, 
                           imageFXComponent->inputPort);
        
        imageFXTunnel = new Tunnel();
        imageFXTunnel->init(imageFXComponent, 
                           imageFXComponent->outputPort, 
                           schedulerComponent, 
                           schedulerComponent->inputPort);
    }
    else
    {
        decoderTunnel = new Tunnel();
        decoderTunnel->init(decoderComponent, 
                           decoderComponent->outputPort, 
                           schedulerComponent, 
                           schedulerComponent->inputPort);
    }
    schedulerTunnel = new Tunnel();

    schedulerTunnel->init(schedulerComponent,
                         schedulerComponent->outputPort,
                         renderComponent,
                         renderComponent->inputPort);
    
    clockTunnel = new Tunnel();

    clockTunnel->init(clockComponent,
                     clockComponent->inputPort + 1,
                     schedulerComponent,
                     schedulerComponent->outputPort + 1);

    
	error = decoderComponent->setState(OMX_StateIdle);
	if (error != OMX_ErrorNone)
	{
		ofLogError(__func__) << "decoderComponent OMX_StateIdle FAIL";
		return false;
	}

	OMX_VIDEO_PARAM_PORTFORMATTYPE formatType;
	OMX_INIT_STRUCTURE(formatType);
	formatType.nPortIndex = decoderComponent->inputPort;
	formatType.eCompressionFormat = omxCodingType;

	if (streamInfo.fpsscale > 0 && streamInfo.fpsrate > 0)
	{
		formatType.xFramerate = (long long)(1<<16)*streamInfo.fpsrate / streamInfo.fpsscale;
	}
	else
	{
		formatType.xFramerate = 25 * (1<<16);
	}

	error = decoderComponent->setParameter(OMX_IndexParamVideoPortFormat, &formatType);
    OMX_TRACE(error);

	if(error != OMX_ErrorNone)
	{
		return false;
	}
	


    
	OMX_PARAM_PORTDEFINITIONTYPE portParam;
	OMX_INIT_STRUCTURE(portParam);
	portParam.nPortIndex = decoderComponent->inputPort;

	error = decoderComponent->getParameter(OMX_IndexParamPortDefinition, &portParam);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;

	//int numVideoBuffers = 20; //20 is minimum - can get up to 80
	//portParam.nBufferCountActual = numVideoBuffers;

	portParam.format.video.nFrameWidth  = videoWidth;
	portParam.format.video.nFrameHeight = videoHeight;


	error = decoderComponent->setParameter(OMX_IndexParamPortDefinition, &portParam);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;


	error = clockTunnel->Establish(false);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;


	OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE concanParam;
	OMX_INIT_STRUCTURE(concanParam);
	concanParam.bStartWithValidFrame = OMX_FALSE;

	error = decoderComponent->setParameter(OMX_IndexParamBrcmVideoDecodeErrorConcealment, &concanParam);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;

	if(NaluFormatStartCodes(streamInfo.codec, extraData, extraSize))
	{
		OMX_NALSTREAMFORMATTYPE nalStreamFormat;
		OMX_INIT_STRUCTURE(nalStreamFormat);
		nalStreamFormat.nPortIndex = decoderComponent->inputPort;
		nalStreamFormat.eNaluFormat = OMX_NaluFormatStartCodes;

		error = decoderComponent->setParameter((OMX_INDEXTYPE)OMX_IndexParamNalStreamFormatSelect, &nalStreamFormat);
        OMX_TRACE(error);
        if(error != OMX_ErrorNone) return false;

	}

	// broadcom omx entension:
	// When enabled, the timestamp fifo mode will change the way incoming timestamps are associated with output images.
	// In this mode the incoming timestamps get used without re-ordering on output images.
    
    //recent firmware will actually automatically choose the timestamp stream with the least variance, so always enable

    OMX_CONFIG_BOOLEANTYPE timeStampMode;
    OMX_INIT_STRUCTURE(timeStampMode);
    timeStampMode.bEnabled = OMX_TRUE;
    error = decoderComponent->setParameter((OMX_INDEXTYPE)OMX_IndexParamBrcmVideoTimestampFifo, &timeStampMode);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;


    
    
    if(doFilters)
    {
        OMX_PARAM_BRCMDISABLEPROPRIETARYTUNNELSTYPE propTunnels;
        OMX_INIT_STRUCTURE(propTunnels);
        propTunnels.nPortIndex = decoderComponent->outputPort;
        error = decoderComponent->getParameter(OMX_IndexParamBrcmDisableProprietaryTunnels, &propTunnels);
        OMX_TRACE(error);
        propTunnels.bUseBuffers = OMX_TRUE;
        error = decoderComponent->setParameter(OMX_IndexParamBrcmDisableProprietaryTunnels, &propTunnels);
        OMX_TRACE(error);
        //ofLogVerbose() << "propTunnels.bUseBuffers: " << propTunnels.bUseBuffers;
    }

    // Alloc buffers for the omx intput port.
    error = decoderComponent->allocInputBuffers();
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;


	error = decoderTunnel->Establish(false);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;

	error = decoderComponent->setState(OMX_StateExecuting);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;

    
    
    if(doFilters)
    {
        
        
        error = imageFXTunnel->Establish(false);
        OMX_TRACE(error);
        if(error != OMX_ErrorNone) return false;
        
        error = imageFXComponent->setState(OMX_StateExecuting);
        OMX_TRACE(error);
        if(error != OMX_ErrorNone) return false;
        
        filterManager.setup(imageFXComponent);
        filterManager.setFilter(settings.filter);
        OMX_TRACE(error);
        if(error != OMX_ErrorNone) return false;
        
    }
    
	error = schedulerTunnel->Establish(false);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;

	error = schedulerComponent->setState(OMX_StateExecuting);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;

    
	// Alloc buffers for the renderComponent input port.
	error = renderComponent->allocInputBuffers();
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;
	
	
	error = renderComponent->setState(OMX_StateIdle);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;
	

	renderComponent->enablePort(renderComponent->outputPort);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;


	OMX_BUFFERHEADERTYPE* eglBuffer = NULL;
	error = renderComponent->useEGLImage(&eglBuffer, renderComponent->outputPort, NULL, eglImage);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;


	if(sendDecoderConfig())
	{
		//ofLogVerbose(__func__) << "sendDecoderConfig PASS";
	}
	else
	{
		ofLog(OF_LOG_ERROR, "sendDecoderConfig FAIL");
		return false;
	}


    renderComponent->listener = this;
	error = renderComponent->setState(OMX_StateExecuting);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;
    
	error = renderComponent->FillThisBuffer(eglBuffer);
    OMX_TRACE(error);
    if(error != OMX_ErrorNone) return false;

	isOpen           = true;
	doSetStartTime      = true;

	isFirstFrame   = true;
	// start from assuming all recent frames had valid pts
	validHistoryPTS = ~0;
	return true;
}

