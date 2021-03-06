#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "video_play.h"
#include "audio_play.h"
#include "io_tools.h"
#include "SDL/SDL.h"

#include "player_client.h"
#include "lib/linkedlist.h"

#include <sys/timeb.h>

#include <libavutil/avutil.h>
#include <libavutil/log.h>
#include <libavutil/avstring.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>


#include "structs.h"
#include "player.h"

int seconds_elapsed;
int paused;

int bytes_received;
pthread_mutex_t bytes_received_mutex = PTHREAD_MUTEX_INITIALIZER;

struct timeb startTime;
struct timeb endTime;
int frameCount;

// Global variables
PacketQueue audioq;
SDL_Surface     *screen;
uint64_t global_video_pkt_pts = AV_NOPTS_VALUE;

pthread_t keyboard_thread_id;
pthread_t stats_thread_id;

void onExit()
{
  global_video_state->quit = 1;
  ftime(&endTime);
  float fps = frameCount / (float)(endTime.time - startTime.time);
  printf("Playback Frame Rate: %.2f fps\n", fps);
  printf("[MAIN] CTRL+C has been received. Add logic here before the program exits\n");
  //frames_to_play = 0;
  //pthread_join(keyboard_thread_id, NULL);
  exit(0);
}

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
	//printf("Queue> AVPacket: 0x%x\n", pkt);
//	printf("+1 Size: %d\n", q->nb_packets);
  AVPacketList *pkt1;
 /* if(av_dup_packet(pkt) < 0) {
    return -1;
  }*/
  pkt1 = av_malloc(sizeof(AVPacketList));
  if (!pkt1)
    return -1;
  pkt1->pkt = *pkt;
  pkt1->next = NULL;
  
  SDL_LockMutex(q->mutex);
  
  if (!q->last_pkt)
    q->first_pkt = pkt1;
  else
    q->last_pkt->next = pkt1;
  q->last_pkt = pkt1;
  q->nb_packets++;
  q->size += pkt1->pkt.size;
  if( SDL_CondSignal(q->cond) == -1){
 	perror("SDL_SIGNAL"); 
  }
  
  SDL_UnlockMutex(q->mutex);
  return 0;
}

//quit = 0;

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {

	//printf("-1 Size: %d\n", q->nb_packets);
  AVPacketList *pkt1;
  int ret = 0;
  
  SDL_LockMutex(q->mutex);
  
    pkt1 = q->first_pkt;
	if (pkt1) {
	  printf("TRUE\n");
      q->first_pkt = pkt1->next;
	  printf("next = %x\n", pkt1->next);
      if (!q->first_pkt)
        q->last_pkt = NULL;
      q->nb_packets--;
      q->size -= pkt1->pkt.size;
      *pkt = pkt1->pkt;
      av_free(pkt1);
      ret = 1;
    }
  SDL_UnlockMutex(q->mutex);
  return ret;
}

/* These are called whenever we allocate a frame buffer. We use this to store 
   the global_pts in a frame at the time it is allocated. 
*/
int our_get_buffer(struct AVCodecContext *c, AVFrame *pic) {
  int ret = avcodec_default_get_buffer(c, pic);
  uint64_t *pts = av_malloc(sizeof(uint64_t));
  *pts = global_video_pkt_pts;
  pic->opaque = pts;
  return ret;
}
void our_release_buffer(struct AVCodecContext *c, AVFrame *pic) {
  if(pic) av_freep(&pic->opaque);
  avcodec_default_release_buffer(c, pic);
}

double synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {

  double frame_delay;

  if(pts != 0) {
    // if we have pts, set video clock to it
    is->video_clock = pts;
  } else {
    // if we aren't given a pts, set it to the clock
    pts = is->video_clock;
  }
  // update the video clock
  frame_delay = av_q2d(is->video_ctx->time_base);
  // if we are repeating a frame, adjust clock accordingly
  frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
  is->video_clock += frame_delay;
  return pts;
}

int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size, double *pts_ptr) {

  int len1, data_size, n;
  AVPacket *pkt = &is->audio_pkt;
  double pts;

  for(;;) {
    while(is->audio_pkt_size > 0) {
      data_size = buf_size;
      len1 = avcodec_decode_audio3(is->audio_ctx, 
				  (int16_t *)audio_buf, &data_size, pkt);
				  //is->audio_pkt_data, is->audio_pkt_size);

      //len1 = avcodec_decode_audio2(aCodecCtx, (int16_t *)audio_buf, &data_size, 
			//	  audio_pkt_data, audio_pkt_size);
      if(len1 < 0) {
        // if error, skip frame
        is->audio_pkt_size = 0;
        break;
      }
      is->audio_pkt_data += len1;
      is->audio_pkt_size -= len1;
      if(data_size <= 0) {
        // No data yet, get more frames
        continue;
      }
      pts = is->audio_clock;
      *pts_ptr = pts;
      //n = 2 * is->audio_ctx->channels;
      n = 2;
      is->audio_clock += (double)data_size /
      (double)(n * is->audio_ctx->sample_rate);

      // We have data, return it and come back for more later
      return data_size;
    }
    if(pkt->data)
      av_free_packet(pkt);

    if(is->quit) {
      return -1;
    }

    // next packet
    if(packet_queue_get(&is->audioq, pkt, 1) < 0) {
      return -1;
    }

    is->audio_pkt_data = pkt->data;
    is->audio_pkt_size = pkt->size;

    // if update, update the audio clock w/pts
    if(pkt->pts != AV_NOPTS_VALUE) {
      is->audio_clock = av_q2d(is->audio_ctx->time_base)*pkt->pts;
    }

  }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {

  VideoState *is = (VideoState *)userdata;
  int len1, audio_size;
  double pts;

  while(len > 0) {
    if(is->audio_buf_index >= is->audio_buf_size) {

      // We have already sent all our data; get more
      audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf), &pts);

      if(audio_size < 0) {
        // If error, output silence
        is->audio_buf_size = 1024;
        memset(is->audio_buf, 0, is->audio_buf_size);
      } else {
        is->audio_buf_size = audio_size;
      }

      is->audio_buf_index = 0;
    }

    len1 = is->audio_buf_size - is->audio_buf_index;
    if(len1 > len)
      len1 = len;

    memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
    len -= len1;
    stream += len1;
    is->audio_buf_index += len1;
  }
}

int decode_interrupt_cb(void) {
  return (global_video_state && global_video_state->quit);
}

//int frames_to_play = 5;

void usage()
{
    printf("\n\
    ./player USERNAME\n\
    Connects to USERNAME. Press CTRL+C to exit\n\n");
}

int queue_picture(VideoState *is, AVFrame *pFrame, double pts) {

  VideoPicture *vp;
  int dst_pix_fmt;
  AVPicture pict;

  /* wait until we have space for a new pic */
  SDL_LockMutex(is->pictq_mutex);
  while(is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
	!is->quit) {
    SDL_CondWait(is->pictq_cond, is->pictq_mutex);
  }
  SDL_UnlockMutex(is->pictq_mutex);

  if(is->quit)
    return -1;

  // windex is set to 0 initially
  vp = &is->pictq[is->pictq_windex];

  // allocate or resize the buffer!
  if(!vp->bmp ||
     vp->width != is->video_ctx->width ||
     vp->height != is->video_ctx->height) {
    SDL_Event event;

    vp->allocated = 0;
    // Send event to the main thread so we can allocate the picture
    event.type = FF_ALLOC_EVENT;
    event.user.data1 = is;
    SDL_PushEvent(&event);

    // wait until we have a picture allocated
    SDL_LockMutex(is->pictq_mutex);
    while(!vp->allocated && !is->quit) {
      SDL_CondWait(is->pictq_cond, is->pictq_mutex);
    }
    SDL_UnlockMutex(is->pictq_mutex);
    if(is->quit) {
      return -1;
    }
  }

  if(vp->bmp) {

    SDL_LockYUVOverlay(vp->bmp);
    
    dst_pix_fmt = PIX_FMT_YUV420P;

    // point pict at the queue
    pict.data[0] = vp->bmp->pixels[0];
    pict.data[1] = vp->bmp->pixels[2];
    pict.data[2] = vp->bmp->pixels[1];
    
    pict.linesize[0] = vp->bmp->pitches[0];
    pict.linesize[1] = vp->bmp->pitches[2];
    pict.linesize[2] = vp->bmp->pitches[1];
    
    // Convert the image into YUV format that SDL uses
    static struct SwsContext *img_convert_ctx;
    img_convert_ctx = sws_getContext(is->video_ctx->width, is->video_ctx->height, 
                    PIX_FMT_YUV420P, 
                    is->video_ctx->width, is->video_ctx->height, PIX_FMT_YUV420P, SWS_BICUBIC, 
                    NULL, NULL, NULL);

    sws_scale(img_convert_ctx, pFrame->data, 
           pFrame->linesize, 0, 
           is->video_ctx->height, 
           pict.data, pict.linesize);
    
    SDL_UnlockYUVOverlay(vp->bmp);

    vp->pts = pts;

    /* now we inform our display thread that we have a pic ready */
    if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
      is->pictq_windex = 0;
    }
    SDL_LockMutex(is->pictq_mutex);
    is->pictq_size++;
    SDL_UnlockMutex(is->pictq_mutex);
  }
  return 0;
}   

int video_thread(void *arg) {
  VideoState *is = (VideoState *)arg;
  AVPacket pkt1, *packet = &pkt1;
  int len1, frameFinished;
  AVFrame *pFrame;
  double pts;

  pFrame = avcodec_alloc_frame();

  for(;;) {
    // if we stopped getting packets
    while( packet_queue_get(&is->videoq, packet, 1) == 0 ) {
    }

    pts = 0;
    // Save global pts to be stored in pFrame in first call
    global_video_pkt_pts = packet->pts;

    // Decode video frame
    //frameCount++;
    len1 = avcodec_decode_video2(is->video_ctx, pFrame, &frameFinished, packet );

    if(packet->dts != AV_NOPTS_VALUE) {
      pts = packet->dts;
    } else {
      pts = 0;
    }
    pts *= av_q2d(is->video_ctx->time_base);


    // Frame is decoded, queue it to be played
    if(frameFinished) {
      pts = synchronize_video( is, pFrame, pts);
      if(queue_picture(is, pFrame, pts) < 0) {
        break;
      }
    }

    // Cleanup...
    av_free_packet(packet);
  }
  av_free(pFrame);
  return 0;
}

int stream_component_open(VideoState *is, int stream_index) {
//int stream_component_open(VideoState *is, AVCodecContext* codecCtx) {

  AVFormatContext *pFormatCtx = is->pFormatCtx;
  AVCodecContext *codecCtx;
  AVCodec *codec;
  SDL_AudioSpec wanted_spec, spec;

  if(stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
    return -1;
  }

  // Get a pointer to the codec context for the video stream
  codecCtx = pFormatCtx->streams[stream_index]->codec;

  if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
    fprintf(stderr, "sample rate: %d\n", codecCtx->sample_rate);
    fprintf(stderr, "channels: %d\n", codecCtx->channels);
    // Set audio settings from codec info
    wanted_spec.freq = codecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = codecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = 1024; //SDL audio buffer size
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = is;
    
    if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
      fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
      return -1;
    }
  }
  fprintf(stderr, "codec id = %d\n", codecCtx->codec_id);
  codec = avcodec_find_decoder(codecCtx->codec_id);
  if(!codec ) {
    fprintf(stderr, "Codec is false!\n");
    return -1;
  }
    
	/*
  if( avcodec_open(codecCtx, codec) < 0) {
    fprintf(stderr, "Unsupported codec!\n");
//    return -1;
  } */
  printf("codec type: %d\n", codecCtx->codec_type);
  if( codecCtx->codec_id  == 13)
  	codecCtx->codec_type = CODEC_TYPE_VIDEO;
  else if( codecCtx->codec_id == 86016)
  	codecCtx->codec_type = AVMEDIA_TYPE_AUDIO;

  switch(codecCtx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      is->audioStream = stream_index;
      is->audio_ctx = pFormatCtx->streams[stream_index];
      //is->audio_ctx = codecCtx;
      is->audio_buf_size = 0;
      is->audio_buf_index = 0;
      memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
      packet_queue_init(&is->audioq);
      SDL_PauseAudio(0);
      break;

    case CODEC_TYPE_VIDEO:
      is->videoStream = stream_index;
      is->video_ctx = pFormatCtx->streams[stream_index];
      //is->video_ctx = codecCtx;

      // Initialize timer stuff
      is->frame_timer = (double)av_gettime() / 1000000.0;
      is->frame_last_delay = 40e-3;
      
      packet_queue_init(&is->videoq);
      is->video_tid = SDL_CreateThread(video_thread, is);

//	  printf("[PLAYER] launched video thread %d\n", is->video_tid);

      // Custom buffer allocation functions
      codecCtx->get_buffer = our_get_buffer;
      codecCtx->release_buffer = our_release_buffer;

      break;

    default:
      break;
  }

  return 0;
}


int decode_thread( void *thread_arg )
{
  VideoState *is = (VideoState *) thread_arg;
  AVFormatContext *pFormatCtx;
  AVPacket pkt1, *packet = &pkt1;
  //global_video_state = is;
  url_set_interrupt_cb(decode_interrupt_cb); // will interrupt blocking functions if we quit!

//  is->filename[0] = *"./foo.mkv";
  strcpy(is->filename, "./foo.mkv");

  int i;
  //AVCodecContext *pCodecCtx;
  //AVCodecContext *aCodecCtx;

  // Open video file
  if( av_open_input_file(&pFormatCtx, is->filename, NULL, 0, NULL) != 0 )
    return -1; // Couldn't open file
  is->pFormatCtx = pFormatCtx;

  // Retrieve stream information
  if(av_find_stream_info(pFormatCtx)<0)
    return -1; // Couldn't find stream information

  // Dump information about file onto standard error
  //dump_format(pFormatCtx, 0, is->filename, 0);

  // Find the video stream and audio stream
  int videoStream = -1;
  int audioStream = -1;
  is->videoStream = -1;
  is->audioStream = -1;
  for( i=0; i<pFormatCtx->nb_streams; i++ )
  {
    if( pFormatCtx->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO ) {
      videoStream = i;
    }
    if( pFormatCtx->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO && audioStream < 0 ) {
      audioStream = i;
    }
  }

  if(videoStream == -1)
    return -1; // Didn't find a video stream
  if(audioStream == -1)
    printf( "No audio stream\n" );
  else
    stream_component_open( is, audioStream );

  stream_component_open( is, videoStream );

  if(is->videoStream < 0 ) {
    fprintf(stderr, "%s: could not open codecs\n", is->filename);
    goto fail;
  } 

  // Decode loop
  for(;;) {
    if(is->quit) {
      break;
    }
    if(is->audioq.size > MAX_AUDIOQ_SIZE ||
       is->videoq.size > MAX_VIDEOQ_SIZE) {
      SDL_Delay(10);
      continue;
    }
/*
    if( av_read_frame(is->pFormatCtx, packet) < 0 ) {
      if( url_ferror((ByteIOContext *)&pFormatCtx->pb) == 0 ) {
        SDL_Delay(100); // no error; wait for user input
        continue;
      } else {
        break;
      }
    }

    // Queue the packets into the right queue
    if(packet->stream_index == is->videoStream) {
      packet_queue_put(&is->videoq, packet);
    } else if(packet->stream_index == is->audioStream) {
      packet_queue_put(&is->audioq, packet);
    } else {
      av_free_packet(packet);
    }
*/
  }

  while(!is->quit) {
    SDL_Delay(100);
  }

 fail:
  if(1){
    SDL_Event event;
    event.type = FF_QUIT_EVENT;
    event.user.data1 = is;
    SDL_PushEvent(&event);
  }
  return 0;

} // End decode_thread()


void alloc_picture(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;

  vp = &is->pictq[is->pictq_windex];
  if(vp->bmp) {
    // we already have one make another, bigger/smaller
    SDL_FreeYUVOverlay(vp->bmp);
  }

  // Allocate a place to put our YUV image on that screen
  if( !screen )
    perror("screen is false");

  vp->bmp = SDL_CreateYUVOverlay(is->video_ctx->width,
				 is->video_ctx->height, SDL_YV12_OVERLAY, screen);
  vp->width = is->video_ctx->width;
  vp->height = is->video_ctx->height;
  
  SDL_LockMutex(is->pictq_mutex);
  vp->allocated = 1;
  SDL_CondSignal(is->pictq_cond);
  SDL_UnlockMutex(is->pictq_mutex);
}

// Callback function to update the video display
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
  SDL_Event event;
  event.type = FF_REFRESH_EVENT;
  event.user.data1 = opaque;
  SDL_PushEvent(&event); // Call the event
  return 0; // 0 means stop timer
}

// schedule a video refresh in 'delay' ms
static void schedule_refresh(VideoState *is, int delay) {
  SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

void video_display(VideoState *is) {
  ftime(&endTime); // Update the endTime; the last displayed frame will finally represent the real end time

  SDL_Rect rect;
  VideoPicture *vp;
  //AVPicture pict;
  float aspect_ratio;
  int w, h, x, y;
  int i;
  i = 0;

  vp = &is->pictq[is->pictq_rindex];
  if(vp->bmp) {
    if(is->video_ctx->sample_aspect_ratio.num == 0) {
      aspect_ratio = 0;
    } else {
      aspect_ratio = av_q2d(is->video_ctx->sample_aspect_ratio) *
        is->video_ctx->width / is->video_ctx->height;
    }
    if(aspect_ratio <= 0.0) {
      aspect_ratio = (float)is->video_ctx->width /
        (float)is->video_ctx->height;
    }
    h = screen->h;
    w = ((int)rint(h * aspect_ratio)) & -3;
    if(w > screen->w) {
      w = screen->w;
      h = ((int)rint(w / aspect_ratio)) & -3;
    }
    x = (screen->w - w) / 2;
    y = (screen->h - h) / 2;
    
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_DisplayYUVOverlay(vp->bmp, &rect);
  }
}

double get_audio_clock(VideoState *is) {
  double pts;
  int hw_buf_size, bytes_per_sec, n;
  
  pts = is->audio_clock; /* maintained in the audio thread */
  hw_buf_size = is->audio_buf_size - is->audio_buf_index;
  bytes_per_sec = 0;
  n = is->audio_ctx->channels * 2;
  if(is->audio_ctx) {
    bytes_per_sec = is->audio_ctx->sample_rate * n;
  }
  if(bytes_per_sec) {
    pts -= (double)hw_buf_size / bytes_per_sec;
  }
  return pts;
}

void video_refresh_timer(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;
  double actual_delay, delay, sync_threshold, ref_clock, diff;
  
  if(is->video_ctx) {
    if(is->pictq_size == 0) {
      schedule_refresh(is, 1);
    } else {
      vp = &is->pictq[is->pictq_rindex];

      delay = vp->pts - is->frame_last_pts; /* the pts from last time */
      if(delay <= 0 || delay >= 1.0) {
        /* if incorrect delay, use previous one */
        delay = is->frame_last_delay;
      }
      /* save for next time */
      is->frame_last_delay = delay;
      is->frame_last_pts = vp->pts;

      /* update delay to sync to audio */
      ref_clock = get_audio_clock(is);
      diff = vp->pts - ref_clock;

      /* Skip or repeat the frame. Take delay into account
         FFPlay still doesn't "know if this is the best guess." */
      sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
      if(fabs(diff) < AV_NOSYNC_THRESHOLD) {
        if(diff <= -sync_threshold) {
          delay = 0;
        } else if(diff >= sync_threshold) {
          delay = 2 * delay;
        }
      }
      is->frame_timer += delay;
      /* computer the REAL delay */
      actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
      if(actual_delay < 0.010) {
        /* Really it should skip the picture instead */
        actual_delay = 0.010;
      }
      schedule_refresh(is, (int)(actual_delay * 1000 + 0.5));
      /* show the picture! */
      video_display(is);
      
      /* update queue for next picture! */
      if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
        is->pictq_rindex = 0;
      }

      SDL_LockMutex(is->pictq_mutex);
      is->pictq_size--;
      SDL_CondSignal(is->pictq_cond);
      SDL_UnlockMutex(is->pictq_mutex);
    }
  } else {
    schedule_refresh(is, 100);
  }
}

void * captureKeyboard(){
	while( stopRecording == 0){
		keyboard_send();
	}
	pthread_exit(NULL);
}

void* calculate_player_stats()
{
  int current_bandwidth;
  int current_latency;
	while( global_video_state->quit == 0)
  {
    seconds_elapsed++;
    pthread_mutex_lock(&bytes_received_mutex);
    current_bandwidth = bytes_received*8;
    bytes_received = 0;
    pthread_mutex_unlock(&bytes_received_mutex);

    printf("[%ds] Incoming Bandwidth: %dbps\n", seconds_elapsed, current_bandwidth);
    if( !paused )
      printf("[%ds] Display Latency: %dms\n", seconds_elapsed, current_latency);
    sleep(1);
	}
	pthread_exit(NULL);
}

int main(int argc, char*argv[])
{
  if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
  {
    usage();
    return 0;
  }
	signal( SIGINT, &onExit);

  ftime(&startTime);

  av_register_all();

  paused = 0;

  // Create space for all the video data
  VideoState      *is;
  is = av_mallocz(sizeof(VideoState));

  // Set up SDL window
  SDL_Event    event;
  memset(&event, 0, sizeof(SDL_Event));

  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  screen = SDL_SetVideoMode(640, 480, 24, 0);

  if(!screen) {
    fprintf(stderr, "SDL: could not set video mode - exiting\n");
    exit(1);
  }

	char * name = malloc(16);
	if(argc > 1){
		strcpy(name, argv[1]);
  }
	else
	{
	  printf("Enter target name of the server: ");
	  scanf("%s", name);
	}
  char * ip = nameserver_init(name);
  free(name);

  // Get the audio & video stream information
  parse_nameserver_msg(ip);

  global_video_state = is;
  //init_gis(global_video_state);
  init_gis(is);
  establish_peer_connections();

	//control_packet* cp = read_control_packet();
  // Initialize the audio & video streams
  /*
  stream_component_open(global_video_state, &cp->audio_codec_ctx);
  printf("[PLAYER] Opened Audio stream\n");
  stream_component_open(global_video_state, &cp->video_codec_ctx);
  printf("[PLAYER] Opened Video stream\n");
  */

	is->quit = 0;

  // Create thread locks
  is->pictq_mutex = SDL_CreateMutex();
  is->pictq_cond = SDL_CreateCond();

  schedule_refresh(is, 40);

  // Create the decode thread
  is->parse_tid = SDL_CreateThread(decode_thread, is);
  printf("[PLAYER] SDL intialized\n");

/*
  if(!is->parse_tid) {
    av_free(is);
    return -1;
  }
  */

	pthread_create(&keyboard_thread_id, NULL,  captureKeyboard, NULL);
	pthread_create(&stats_thread_id, NULL,  calculate_player_stats, NULL);
//	printf("[PLAYER] Keyboard thread :%d\n", keyboard_thread_id);

//  while(global_video_state->quit == 0) 
  while(1) {
    SDL_WaitEvent(&event);
    switch(event.type)
    {
      // Done decoding
      case FF_QUIT_EVENT:
      case SDL_QUIT:
        is->quit = 1;
        exit(0);
        break;
      // Get frame
      case FF_ALLOC_EVENT:
        alloc_picture(event.user.data1);
        break;

      // Refresh screen
      case FF_REFRESH_EVENT:
        video_refresh_timer(event.user.data1);
        break;

      default:
        break;
    }
  }

  printf("Frames per second: %i\n", 5);
  printf("[MAIN] Quit player\n");
  return 0;
}

