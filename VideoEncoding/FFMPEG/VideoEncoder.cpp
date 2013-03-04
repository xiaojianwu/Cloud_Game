#include "VideoEncoder.h"


VideoEncoder::VideoEncoder()
{
	lpvMem=NULL;
	hMapObject=NULL;
	//=========Remebem to free above var
	codec=NULL;
    c= NULL;
	picture=NULL;
	outbuf=NULL;
	picture_buf=NULL;
	//=================================
	opt = NULL;
	workingThread=false;
}
VideoEncoder::~VideoEncoder()
{
	if(c!=NULL)
	{
		avcodec_close(c);
		av_free(c);
		c=NULL;
	}
	if(picture!=NULL)
	{
		av_free(picture);
		picture=NULL;
	}
	if(picture_buf!=NULL)
	{
		free(picture_buf);
		picture_buf=NULL;
	}
	if(outbuf!=NULL)
	{
		free(outbuf);
		outbuf=NULL;
	}
	uninstallSharedMemory();
}
bool VideoEncoder::isMemoryReadable()
{
	return lpvMem[SHAREDMEMSIZE/8-1]==1;
}
void VideoEncoder::setMemoryWritable()
{
	lpvMem[SHAREDMEMSIZE/8-1]=0;
}
void VideoEncoder::uninstallSharedMemory()
{
	if(lpvMem!=NULL)
	UnmapViewOfFile(lpvMem);
	lpvMem=NULL;
	if(hMapObject!=NULL)
	CloseHandle(hMapObject);
	hMapObject=NULL;
	
	
}
bool VideoEncoder::setupSharedMemory()
{
		hMapObject = CreateFileMapping( 
                INVALID_HANDLE_VALUE,   // use paging file
                NULL,                   // default security attributes
                PAGE_READWRITE,         // read/write access
                0,                      // size: high 32-bits
                SHAREDMEMSIZE,              // size: low 32-bits
                TEXT("ded9dllmemfilemap")); // name of map object
        if (this->hMapObject == NULL) 
            return FALSE; 
		bool fInit = (GetLastError() != ERROR_ALREADY_EXISTS); 
		lpvMem = (BYTE*)MapViewOfFile( 
                hMapObject,     // object to map view of
                FILE_MAP_WRITE, // read/write access
                0,              // high offset:  map from
                0,              // low offset:   beginning
                0);             // default: map entire file
		if (this->lpvMem == NULL) 
           return FALSE; 
		if (fInit) 
           memset(lpvMem, '\0', SHAREDMEMSIZE);
		
		return TRUE;
}
bool VideoEncoder::initVideoCodec()
{
	avcodec_register_all();
	codec = avcodec_find_encoder(CODEC_ID_H264);
    if (!codec) 
	{
        printf( "codec not found/n");
		return false;
    }
	av_dict_set(&opt, "vprofile", "baseline", 0);
	av_dict_set(&opt, "tune", "zerolatency", 0);
	av_dict_set(&opt, "preset","ultrafast",0);
    c= avcodec_alloc_context();
	/* put sample parameters */
    c->bit_rate = 300000;
    /* resolution must be a multiple of two */
    c->width = 720;
    c->height = 480;
    /* frames per second */
    c->time_base.num = 1; 
	c->time_base.den = 25;
    c->gop_size = 10; /* emit one intra frame every ten frames */
    c->max_b_frames=1;
    c->pix_fmt = PIX_FMT_YUV420P;
	c->me_range = 16;
    c->max_qdiff = 4;
    c->qmin = 10;
    c->qmax = 51;
    c->qcompress = (0.6f);
	c->refs=1;
	c->dia_size=1;
	c->keyint_min=48;
	c->thread_type=CODEC_CAP_SLICE_THREADS;
	c->thread_count=4;
	c->slices=4;
	c->slice_count=4;
    if (avcodec_open2(c, codec,&opt) < 0) 
	{
        printf( "could not open codec\n");
        return false;
    }
	picture= avcodec_alloc_frame();
	return true;
}
void VideoEncoder::debugEncoder(const char *filename)
{
    int i, out_size, size, x, y, outbuf_size;
    FILE *f;
	f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "could not open %s/n", filename);
        exit(1);
    }

    /* alloc image and output buffer */
    outbuf_size = 100000;
    outbuf = (uint8_t *)malloc(outbuf_size);
    size = c->width * c->height;
    picture_buf =(uint8_t *) malloc((size * 3) / 2); /* size for YUV 420 */

    picture->data[0] = picture_buf;
    picture->data[1] = picture->data[0] + size;
    picture->data[2] = picture->data[1] + size / 4;
    picture->linesize[0] = c->width;
    picture->linesize[1] = c->width / 2;
    picture->linesize[2] = c->width / 2;
	picture->pts=0;
    /* encode 1 second of video */
    for(i=0;i<25;i++) {
        fflush(stdout);
        /* prepare a dummy image */
        /* Y */
        for(y=0;y<c->height;y++) {
            for(x=0;x<c->width;x++) {
                picture->data[0][y * picture->linesize[0] + x] = x + y + i * 3;
            }
        }

        /* Cb and Cr */
        for(y=0;y<c->height/2;y++) {
            for(x=0;x<c->width/2;x++) {
                picture->data[1][y * picture->linesize[1] + x] = 128 + y + i * 2;
                picture->data[2][y * picture->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        /* encode the image */
        out_size = avcodec_encode_video(c, outbuf, outbuf_size, picture);
		picture->pts ++;
        printf("encoding frame %3d (size=%5d)/n", i, out_size);
        fwrite(outbuf, 1, out_size, f);
    }

    /* get the delayed frames */
    for(; out_size; i++) {
        fflush(stdout);

        out_size = avcodec_encode_video(c, outbuf, outbuf_size, NULL);
        printf("write frame %3d (size=%5d)/n", i, out_size);
        fwrite(outbuf, 1, out_size, f);
    }
	fclose(f);
}
bool VideoEncoder::initEncoder()
{
	if(!setupSharedMemory())
	{
		printf("Memory Failed\n");
		return false;
	}
	if(!initVideoCodec())
	{
		printf("Codec Failed\n");
		return false;
	}
	printf("Video Encoder Init Great!\n");
	workingThread=true;
	return true;
}
void VideoEncoder::encodeFrameLoop()
{
	while(workingThread)
	{
		if(isMemoryReadable())
		{
			int copySize=0;
			int height=0;
			int width=0;
			int bpp=0;
			memcpy((void *)&copySize,lpvMem+(SHAREDMEMSIZE-RESERVEDMEMORY)/8,sizeof(int));
			memcpy((void *)&height,lpvMem+(SHAREDMEMSIZE-RESERVEDMEMORY)/8+sizeof(height),sizeof(height));
			memcpy((void *)&width,lpvMem+(SHAREDMEMSIZE-RESERVEDMEMORY)/8+sizeof(height)*2,sizeof(width));
			memcpy((void *)&bpp,lpvMem+(SHAREDMEMSIZE-RESERVEDMEMORY)/8+sizeof(height)*3,sizeof(bpp));
			printf("%d bytes height:%d width:%d bpp:%d\n",copySize,height,width,bpp);
			setMemoryWritable();
		}
		else
		{
			Sleep(1000/MAXFPS);
		}
	}
}
void VideoEncoder::stopEncode()
{
	workingThread=false;
}
int _tmain(int argc, _TCHAR* argv[])
{
	
	VideoEncoder encoder;
	encoder.initEncoder();
	//encoder.encodeFrameLoop();
	encoder.debugEncoder("c:/test1.avi");
	return 0;
}
