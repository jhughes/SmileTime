#include "video_record.h"

#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <asm/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include <libavutil/avutil.h>
#include <libavutil/log.h>
#include <libavutil/avstring.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#define BUFFERCOUNT 1

#ifndef V4L2_PIX_FMT_PJPG
#define V4L2_PIX_FMT_PJPG v4l2_fourcc('P', 'J', 'P', 'G')
#endif

#ifndef V4L2_PIX_FMT_RGB
#define V4L2_PIX_FMT_RGB v4l2_fourcc('G', 'B', 'R', 'G')
#endif

#ifndef V4L2_PIX_FMT_MJPG
#define V4L2_PIX_FMT_MJPG v4l2_fourcc('M', 'J', 'P', 'G')
#endif

#ifndef V4L2_PIX_FMT_JPEG
#define V4L2_PIX_FMT_JPEG v4l2_fourcc('J', 'P', 'E', 'G')
#endif

#ifndef V4L2_CID_PAN_RELATIVE 
//#define V4L2 _CID_PAN_RELATIVE (V4L2 _CID_PRIVATE_BASE+7) 
#define V4L2_CID_PAN_RELATIVE 0x009A0904 
#endif

#ifndef V4L2_CID_TILT_RELATIVE 
//#define V4L2 _CID_TILT_RELATIVE (V4L2 _CID_PRIVATE_BASE+8) 
#define V4L2_CID_TILT_RELATIVE 0x009A0905 
#endif

#ifndef V4L2_CID_PANTILT_RESET 
//#define V4L2 _CID_PANTILT_RESET (V4L2 _CID_PRIVATE_BASE+9) 
#define V4L2_CID_PANTILT_RESET 0x0A046D03 
#endif


//SOURCES:
/*
http://v4l2spec.bytesex.org/spec/book1.htm
http://v4l2spec.bytesex.org/spec/capture-example.html
*/

int camera_fd = -1;
char* camera_name = "/dev/video0";
char* filename;
FILE* video_file;

/*
struct buffer {
	void * start;
	size_t length;
};

struct buffer * buffers = NULL;
struct buffer {
    void * start;
    size_t length;
};
struct buffer * buffers = NULL;
*/
//buffers = NULL;



void print_default_crop(){
	// Get information about the video cropping and scaling abilities
	struct v4l2_cropcap crop;
	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // Set the cropping request to be specific to video capture
	if(ioctl(camera_fd, VIDIOC_CROPCAP, &crop)==-1){
		printf("Couldn't get cropping info\n");
		perror("ioctl");
	}
	struct v4l2_rect defaultRect;
	defaultRect = crop.bounds;
	printf("Default cropping rectangle\nLeft: %d, Top: %d\n %dpx by %dpx\n", defaultRect.left, defaultRect.top, defaultRect.width, defaultRect.height);

	int i = 0;
	for(; i < 5; i++){
		struct v4l2_fmtdesc fmtdesc;
		fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmtdesc.index = 1;
		if(ioctl(camera_fd, VIDIOC_ENUM_FMT, &fmtdesc)==-1){
			printf("Format query failed\n");
			perror("ioctl");
		}
		printf("Format: %s\n", fmtdesc.description);
	}
}

void print_input_info(){
	//found these online
	struct v4l2_input input;
	int index;

	if (-1 == ioctl (camera_fd, VIDIOC_G_INPUT, &index)) {
       		perror ("VIDIOC_G_INPUT");
        	exit (EXIT_FAILURE);
	}

	memset (&input, 0, sizeof (input));
	input.index = index;

	if (-1 == ioctl (camera_fd, VIDIOC_ENUMINPUT, &input)) {
   	     perror ("VIDIOC_ENUMINPUT");
  	      exit (EXIT_FAILURE);
	}

	printf ("Current input: %s\n", input.name);

}

void set_format(){
	// Set the format of the image from the video
	struct v4l2_format format;
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix.width = 640;
	format.fmt.pix.height = 480;
	format.fmt.pix.field = V4L2_FIELD_INTERLACED;
//	format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPG;
	format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

	if(ioctl(camera_fd, VIDIOC_S_FMT, &format) == -1){
		perror("VIDIOC_S_FMT");
		exit( EXIT_FAILURE );
	}
	struct v4l2_pix_format pix_format;
	pix_format = format.fmt.pix;
	printf("Image Width: %d\n",pix_format.width);
	if(!(format.fmt.pix.pixelformat & V4L2_PIX_FMT_MJPG)){
		printf("Error: MJPG compressions wasn't set\n");
	}
}

//This function initialize the camera device and V4L2 interface
void video_record_init(char* f){
  filename = f;
  //printf("Filename init: %s", filename);
	//open camera
	camera_fd = open(camera_name, O_RDWR );
	if(camera_fd == -1){
		printf("error opening camera %s\n", camera_name);
		return;
	}
	
	print_Camera_Info();

	set_format();	

	mmap_init();	

  video_file = fopen(filename, "wb");
  if (!f) {
      fprintf(stderr, "could not open %s\n", filename);
      exit(1);
  }


  //printf("[V_REC] This function initialize the camera device and V4L2 interface\n");
}

void read_frame(){

	// DEQUEUE frame from buffer
	struct v4l2_buffer buf;
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	if( ioctl(camera_fd, VIDIOC_DQBUF, &buf) == -1){
		perror("VIDIOC_DQBUF");
	}
	
	//printf("Read buffer index:%d\n", buf.index);

	// WRITE buffer to file
	/*FILE * frame_fd = fopen( "1.jpg", "w+");
	if( fwrite(buffers[buf.index].start, buffers[buf.index].length, 1, frame_fd) != 1)
		perror("fwrite");
	fclose(frame_fd);
  */

	// ENQUEUE frame into buffer
	struct v4l2_buffer bufQ;
	bufQ.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufQ.memory = V4L2_MEMORY_MMAP;
	bufQ.index = buf.index;
	if( ioctl(camera_fd, VIDIOC_QBUF, &bufQ) == -1 ){
		perror("VIDIOC_QBUF");
	}
}

void mmap_init(){
	// Request that the device start using the buffers
	// - Find the number of support buffers
	struct v4l2_requestbuffers reqbuf;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuf.count = BUFFERCOUNT;
	reqbuf.memory = V4L2_MEMORY_MMAP;
	if (ioctl (camera_fd, VIDIOC_REQBUFS, &reqbuf) == -1){
		perror("ioctl");
		exit(EXIT_FAILURE);
	}	
	//printf("Buffer count: %d\n", reqbuf.count);
	
	buffers = malloc( reqbuf.count * sizeof(*buffers));
	int i =0;

	// Mmap memory for each slice of the buffer	
	for(; i < reqbuf.count; i++){
		struct v4l2_buffer buf;
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if( ioctl ( camera_fd, VIDIOC_QUERYBUF, &buf) == -1 ){
			perror("VIDIOC_QUERYBUF");
		}

		//printf("Index: %d, Buffer offset: %d\n", i, buf.m.offset);
		buffers[i].length = buf.length; 
		buffers[i].start = mmap( NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, camera_fd, buf.m.offset);
		if (buffers[i].start == MAP_FAILED){
			perror("mmap");
		}
	}
	
	// Start capturing
	for(i=0; i <reqbuf.count; i++){
		struct v4l2_buffer buf;
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if( ioctl(camera_fd, VIDIOC_QBUF, &buf) == -1 ){
			perror("VIDIOC_QBUF");
		}
	}

	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if( ioctl(camera_fd, VIDIOC_STREAMON, &type) == -1){
		perror("VIDIOC_STREAMON");
	}
	

}

//This function copies the raw image from webcam frame buffer to program memory through V4L2 interface
int video_frame_copy(){

	// DEQUEUE frame from buffer
	struct v4l2_buffer buf;
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	if( ioctl(camera_fd, VIDIOC_DQBUF, &buf) == -1){
		perror("VIDIOC_DQBUF");
	}

	// ENQUEUE frame into buffer
	struct v4l2_buffer bufQ;
	bufQ.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufQ.memory = V4L2_MEMORY_MMAP;
	bufQ.index = buf.index;
	if( ioctl(camera_fd, VIDIOC_QBUF, &bufQ) == -1 ){
		perror("VIDIOC_QBUF");
	}
	return buf.index;
}

// This function should compress the raw image to JPEG image, or MPEG-4 or H.264 frame if you choose to implemente that feature
void video_frame_compress(int index){

   int i, enc_size, x, outbuf_size, inbuf_size;
   FILE *f;
   uint8_t *outbuf, *inbuf;

   AVCodec *codec;
   AVCodecContext *c= NULL;

   i = 0;

   AVFrame *srcFrame, *dstFrame; 

   static struct SwsContext *img_convert_ctx;

   // printf("Video encoding\n");

   // find the h264 video encoder
   codec = avcodec_find_encoder(CODEC_ID_H264);
   if (!codec) {
       fprintf(stderr, "codec not found\n");
       exit(1);
   }

   c = avcodec_alloc_context();

   // sample parameters
   c->bit_rate = 400000;
   c->width = 640;
   c->height = 480;

   // frames parameters
   c->time_base= (AVRational){1,25};
   c->gop_size = 10; // emit one intra frame every ten frames
   c->max_b_frames=1;
   c->pix_fmt = PIX_FMT_YUV420P;

   // h264 parameters
   c->me_range = 16;
   c->max_qdiff = 4;
   c->qmin = 10;
   c->qmax = 51;
   c->qcompress = 0.6; 

   // open the codec
   if (avcodec_open(c, codec) < 0) {
       fprintf(stderr, "could not open codec\n");
       exit(1);
   }

   // Allocate space for the frames
   srcFrame = avcodec_alloc_frame(); 
   dstFrame = avcodec_alloc_frame(); 

   // Create AVFrame for YUV420 frame
   outbuf_size = avpicture_get_size(PIX_FMT_YUV420P, c->width, c->height);
   outbuf = av_malloc(outbuf_size);
   avpicture_fill((AVPicture *)dstFrame, outbuf, PIX_FMT_YUV420P, c->width, c->height);

   // Create AVFrame for YUYV422 frame
   inbuf_size = avpicture_get_size(PIX_FMT_YUYV422, c->width, c->height);
   inbuf = av_malloc(inbuf_size);
   x = avpicture_fill((AVPicture *)srcFrame, buffers[index].start, PIX_FMT_YUYV422, c->width, c->height);

   // Make the conversion context
   img_convert_ctx = sws_getContext(c->width, c->height, 
                    PIX_FMT_YUYV422, 
                    c->width, c->height, PIX_FMT_YUV420P, SWS_BICUBIC, 
                    NULL, NULL, NULL);

   // Convert the image to YUV420
   sws_scale(img_convert_ctx, srcFrame->data, 
         srcFrame->linesize, 0, 
         c->height, 
         dstFrame->data, dstFrame->linesize);

   // Encode the frame
   do { 
     // Why so many empty encodes at the beginning?
     enc_size = avcodec_encode_video(c, outbuf, outbuf_size, dstFrame);
     // printf("encoding frame %3d (size=%5d)\n", i, enc_size);
     fwrite(outbuf, 1, enc_size, video_file);
   } while ( enc_size == 0 );

   // Get the delayed frames
   //for(; enc_size; i++) {
   for(; i<=5; i++) {
     // Why is this necessary?
     fflush(stdout);
     enc_size = avcodec_encode_video(c, outbuf, outbuf_size, NULL);
     // printf("write frame %3d (size=%5d)\n", i, enc_size);
     if( enc_size ) fwrite(outbuf, 1, enc_size, video_file);
   }

   av_free(outbuf);
   av_free(inbuf);

   avcodec_close(c);
   av_free(c);
   printf("\n");

   // encode 1 second of video
   /*for(i=0;i<25;i++) {
       fflush(stdout);
       // prepare a dummy image
       // Y
       for(y=0;y<c->height;y++) {
           for(x=0;x<c->width;x++) {
               picture->data[0][y * picture->linesize[0] + x] = x + y + i * 3;
           }
       }

       // Cb and Cr
       for(y=0;y<c->height/2;y++) {
           for(x=0;x<c->width/2;x++) {
               picture->data[1][y * picture->linesize[1] + x] = 128 + y + i * 2;
               picture->data[2][y * picture->linesize[2] + x] = 64 + x + i * 5;
           }
       }

       // encode the image
       enc_size = avcodec_encode_video(c, outbuf, outbuf_size, picture);
       printf("encoding frame %3d (size=%5d)\n", i, out_size);
       fwrite(outbuf, 1, out_size, f);
   }
   */

   // add sequence end code to have a real mpeg file
   /*outbuf[0] = 0x00;
   outbuf[1] = 0x00;
   outbuf[2] = 0x01;
   outbuf[3] = 0xb7;
   fwrite(outbuf, 1, 4, f);
   */
   pthread_exit(NULL);
}

//Closes the camera and frees all memory
void video_close(){
	//printf("Closing stream");
  fclose(video_file);

  // Free everything
	int closed = close(camera_fd);
	if(closed == 0)
		camera_fd = -1;
}

//find capabilities of camera and print them out
void print_Camera_Info(){
	struct v4l2_capability cap;
	ioctl(camera_fd, VIDIOC_QUERYCAP, &cap);
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)){
                fprintf (stderr, "%s is not a camera\n", camera_name);
                return;
        }
	// Print out basic statistics
	printf("File Descriptor: %d\n", camera_fd);
	printf("Driver: %s\n", cap.driver);
	printf("Device: %s\n", cap.card);
	printf("bus_info: %s\n", cap.bus_info);
	if( !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ){
		printf("No video capture capabilities!\n");
	}
	if( !(cap.capabilities & V4L2_CAP_READWRITE) ){
		printf("No read/write capabilities!\n");
	}
	if( !(cap.capabilities & V4L2_CAP_STREAMING) ){
		printf("No streaming capabilities!\n");
	}
}

//http://www.zerofsck.org/2009/03/09/example-code-pan-and-tilt-your-logitech-sphere-webcam-using-python-module-lpantilt-linux-v4l2/
int pan_relative(int pan)
{
    struct v4l2_ext_control xctrls;
    struct v4l2_ext_controls ctrls;
    xctrls.id = V4L2_CID_PAN_RELATIVE;
    xctrls.value = pan;
    ctrls.count = 1;
    ctrls.controls = &xctrls;
    if ( ioctl(camera_fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0 ) {
        perror("VIDIOC_S_EXT_CTRLS - Pan error. Are the extended controls available?\n");
        return -1;
    } else {
        printf("PAN Success");
    }
    return 0;
}

int tilt_relative(int tilt)
{
    struct v4l2_ext_control xctrls;
    struct v4l2_ext_controls ctrls;
    xctrls.id = V4L2_CID_TILT_RELATIVE;
    xctrls.value = tilt;
    ctrls.count = 1;
    ctrls.controls = &xctrls;
    if ( ioctl(camera_fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0 )
    {
        perror("VIDIOC_S_EXT_CTRLS - Tilt error. Are the extended controls available?\n");
        return -1;
    } else {
        printf("TILT Success");
    }

    return 0;
}

int panTilt_relative(int pan, int tilt)
{
    struct v4l2_ext_control xctrls[2];
    struct v4l2_ext_controls ctrls;
    xctrls[0].id = V4L2_CID_PAN_RELATIVE;
    xctrls[0].value = pan;
    xctrls[1].id = V4L2_CID_TILT_RELATIVE;
    xctrls[1].value = tilt;
    ctrls.count = 2;
    ctrls.controls = xctrls;
    if ( ioctl(camera_fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0 )
    {
        perror("VIDIOC_S_EXT_CTRLS - Pan/Tilt error. Are the extended controls available?\n");
        return -1;
    } else {
        printf("PAN/TILT Success");
    }

    return 0;
}

int panTilt_reset()
{
    struct v4l2_ext_control xctrls;
    struct v4l2_ext_controls ctrls;
    xctrls.id = V4L2_CID_PANTILT_RESET;
    xctrls.value = 1;
    ctrls.count = 1;
    ctrls.controls = &xctrls;
    if ( ioctl(camera_fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0 )
    {
        perror("VIDIOC_S_EXT_CTRLS - Pan/Tilt error. Are the extended controls available?\n");
        return -1;
    } else {
        printf("PAN/TILT reset Success");
    }
	 
    return 0;
}
