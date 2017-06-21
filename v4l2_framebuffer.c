#include <stdio.h>  
#include <stdlib.h>  
#include <string.h>  
#include <assert.h>  
#include <getopt.h>    
#include <fcntl.h>    
#include <unistd.h>  
#include <errno.h>  
#include <sys/stat.h>  
#include <sys/types.h>  
#include <sys/time.h>  
#include <sys/mman.h>  
#include <sys/ioctl.h>  
#include <asm/types.h>  
#include <linux/videodev2.h>  
#include <linux/fb.h>  
#include <stdbool.h>
#include "table_list.h" //rgb list
#include "jpeglib.h"
#include <time.h>
#include <linux/input.h>
#include "bat.h"
#include <signal.h>
#include "ascii.h"
#include <time.h>
#include "recording.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftglyph.h>
#include <math.h>

extern void queryDeviceControlCapability(int fd);

void getFilename(char *base, char *ext, char *result, int len);
void lcd_put_pixel(int x, int y, unsigned int color);
#define CLEAR(x) memset (&(x), 0, sizeof (x))  

#define DBG(x...) printf(x)
#define INFO(x...) printf(x)
#define TSS() gettimeofday(&start_time, NULL)
#define TSE() gettimeofday(&end_time, NULL)
#define TS_DURATION() (1000000 * (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_usec - start_time.tv_usec))


#define WIDTH	640
#define HEIGHT	480
#define FRAME_MEMORY_SIZE  1228800
struct buffer {  
    void * start;  
    size_t length;  
};  

struct timeval start_time, end_time;
static char * dev_name = NULL;  
static int fd = -1;  
struct buffer * buffers = NULL;  
static unsigned int n_buffers = 0;  
static int time_in_sec_capture=5;  
static int fbfd = -1;  
static struct fb_var_screeninfo vinfo;  
static struct fb_fix_screeninfo finfo;  
static char *fbp=NULL;  
static long screensize=0;  
int mjpeg_frame_size = 640 * 480 * 2;
struct jpeg_decompress_struct cinfo;
struct jpeg_error_mgr jerr;
unsigned char *rowp[1];
unsigned char row_tmp[640*3];
bool recordingFlag = false;
bool photosnapshotFlag = false;
bool finalizeVideoFlag = false;
bool updateFramebufferFlag = true;
bool terminateFlag = false;
FILE *fh_video=NULL;
int  frames_to_record=-1;
int  recorded_frames=0;
unsigned int line_width = 1920;
unsigned int pixel_width = 4;

bool playbackFlag = false;
const char *file_to_play=NULL;

unsigned char *bat_label;
unsigned char *fbmem;

static int key_fd = -1;
char datetime[40];
char timestr[40];


#define YELLOW 0xffffff00
#define RED 0xffff0000
FT_Library    library;

FT_Face       face;
FT_Matrix     matrix;                 /* transformation matrix */
FT_Vector     pen;                    /* untransformed origin  */
FT_BBox       bbox;
FT_Glyph      glyph;
unsigned int line_width;
unsigned int pixel_width;
int line_box_ymin=640;
int line_box_ymax=0;
bool warning_flags = false;
wchar_t  warning_label[40];
unsigned int label_color = YELLOW;
void freetype_init()
{
	FT_Error      error;
	double        angle;
	error = FT_Init_FreeType( &library );              /* initialize library */
	/* error handling omitted */
    error = FT_New_Face( library, "/usr/lib/fonts/STXIHEI.TTF", 0, &face ); /* create face object */
	if(!error)
		printf("FT New Freetype Sucess.\n");
	error = FT_Set_Pixel_Sizes( face, 24, 0 );                /* set character size */
	
	angle         = ( 270.0 / 360 ) * 3.14159 * 2;      /* use 25 degrees     */
	/* set up matrix */
	matrix.xx = (FT_Fixed)( cos( angle ) * 0x10000L );
	matrix.xy = (FT_Fixed)(-sin( angle ) * 0x10000L );
	matrix.yx = (FT_Fixed)( sin( angle ) * 0x10000L );
	matrix.yy = (FT_Fixed)( cos( angle ) * 0x10000L );	
}

void draw_bitmap( FT_Bitmap*  bitmap,
             FT_Int      x,
             FT_Int      y)
{
	FT_Int  i, j, p, q;
	FT_Int  x_max = x + bitmap->width;
	FT_Int  y_max = y + bitmap->rows;
	
	
	for ( i = x, p = 0; i < x_max; i++, p++ )
	{
		for ( j = y, q = 0; j < y_max; j++, q++ )
		{
			if ( i < 0      || j < 0       ||
				i >= vinfo.xres || j >= vinfo.yres )
				continue;
			
			//image[j][i] |= bitmap->buffer[q * bitmap->width + p];
			if(bitmap->buffer[q * bitmap->width + p] == 0 )
				continue ;

            lcd_put_pixel(i,j,label_color);
			//lcd_put_pixel(i,j,bitmap->buffer[q * bitmap->width + p]);
		}
	}
}

int show_fonts(int x, int y,  wchar_t *string)
{
	int i;
	FT_Error      error;
	pen.x = (x)*64;
	pen.y = (y)*64;
    for( i=0; i<wcslen(string); i++ )
	{
		/* set transformation */
		FT_Set_Transform( face, &matrix, &pen );
	
		/* load glyph image into the slot (erase previous one) */
        error = FT_Load_Char( face, string[i], FT_LOAD_RENDER );
		if(error)
		{
			printf("FT_load_char error\n");
			return -1;
		}

		error = FT_Get_Glyph(face->glyph, &glyph );
		if(error)
		{
		    printf("FT_Get_Glyph error\n");
		    return -1;
		}
	
		FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_TRUNCATE, &bbox);
		if( line_box_ymin > bbox.yMin )
			line_box_ymin = bbox.yMin;
		if( line_box_ymax < bbox.yMax )
			line_box_ymax = bbox.yMax;
	
		draw_bitmap( &face->glyph->bitmap,
			     face->glyph->bitmap_left,
			     vinfo.yres - face->glyph->bitmap_top );
	
		/* increment pen position */
		pen.x += face->glyph->advance.x;
		pen.y += face->glyph->advance.y;
	}
	
}


/*
#define KEY_DOWN 	1
#define KEY_UP		0
*/	
static void errno_exit (const char * s)  
{  
    fprintf (stderr, "%s error %d, %s\n",s, errno, strerror (errno));  
    exit (EXIT_FAILURE);  
}  

static int xioctl (int fd,int request,void * arg)  
{  
    int r;  
    do r = ioctl (fd, request, arg);  
    while (-1 == r && EINTR == errno);  
    return r;  
}  

int readBattery(const char *path)
{
    int fd;
    int v = -1;

    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        char buffer[20];
        if (read(fd, buffer, sizeof(buffer)) > 0) {
            sscanf(buffer, "%d\n", &v);
        }
        close(fd);
    }
    return v;
}


void lcd_put_pixel(int x, int y, unsigned int color)
{
	unsigned char *pen_8 = fbmem+y*line_width+x*pixel_width;
	unsigned short *pen_16;	
	unsigned int *pen_32;	

	unsigned int red, green, blue;	

	pen_16 = (unsigned short *)pen_8;
	pen_32 = (unsigned int *)pen_8;

	switch (vinfo.bits_per_pixel)
	{
		case 8:
		{
			*pen_8 = color;
			break;
		}
		case 16:
		{
			/* 565 */
			red   = (color >> 16) & 0xff;
			green = (color >> 8) & 0xff;
			blue  = (color >> 0) & 0xff;
			color = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
			*pen_16 = color;
			break;
		}
		case 32:
		{
			*pen_32 = color;
			break;
		}
		default:
		{
			//printf("can't surport %dbpp\n", var.bits_per_pixel);
			break;
		}
	}
}

 void lcd_put_test(int x, int y, unsigned char c)
 {
	 int i,j,k;
	 if(c < '!' || c > '!' + 94)
	 	return ;
	unsigned char *dots = (unsigned char *)&fonts[(c-'!')*36];
	 for (i = 0; i < 12; i++)
	 {
		 for(j=0; j < 3 ; j++)
		 {	
			unsigned char c = dots[3*i+j];
			for(k=0; k<8; k++)
			 {
				if(0x01 & (c >> k))
				 {
					 /* show */
					 lcd_put_pixel(x+k+8*j, y+i, 0xffffff00); /* �� */
				 }
				 else
				 {
					 /* hide */
					 //lcd_put_pixel(x+7-b, y+i, 0); /* �� */
				 }
			}
		}
	 }	 
}
 void lcd_put_string(int x,int y,unsigned char *string)
 {
	 char *ptr = string;
	 
	 while(*ptr  != NULL){
		 lcd_put_test( x, y, *ptr);
		 y += 12;
		 ptr++;
	 }
	 
 }

char *get_current_time()  
{  
	time_t t;  
	struct tm *nowtime;  
	  
	time(&t);  
	nowtime = localtime(&t);  
	strftime(timestr,sizeof(timestr),"%02l:%02M:%02S %p",nowtime);	
	strftime(datetime,sizeof(datetime),"%02d/%02m/%04Y",nowtime);	
	return timestr;  
}  

void show_time()
{
	get_current_time();
	lcd_put_string(50, 480, datetime);
	lcd_put_string(24, 480,timestr);	
}

void show_recording(int x, int y)
{
	 int i,j,k;
	unsigned char *dots = recording_label;
	 for (i = 0; i < 31; i++)
	 {
		 for(j=0; j < 4 ; j++)
		 {	
			unsigned char c = dots[i*4 + j];
			for(k=0; k<8; k++)
			 {
				if(0x01 & (c >> k))
				 {
					 /* show */
					 lcd_put_pixel(x+k+8*j, y+i, 0xff0000); /* �� */
				 }
				 else
				 {
					 /* hide */
					 //lcd_put_pixel(x+7-b, y+i, 0); /* �� */
				 }
			}
		}
	 }	 	
}

bool readBatteryString(const char *path)
{
    int fd;
    bool result = false;
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        char buffer[20];
        if (read(fd, buffer, sizeof(buffer)) > 0) {
            char tmp_buffer[20];
            sscanf(buffer, "%s\n", tmp_buffer);
            if (strcmp("Charging", tmp_buffer) == 0)
                result = true;
            else
                result = false;
        }
        close(fd);
    }
    return result;
}

void update_bat()
{
    int v;
    bool bat_status;
	v = readBattery("/sys/bus/i2c/devices/1-0036/power_supply/battery/capacity");
    bat_status = readBatteryString("/sys/bus/i2c/devices/1-0036/power_supply/battery/status");
	if(v > 75){
		bat_label = battery_100;
        warning_flags = false;
	}else if(v > 50){
		bat_label = battery_75;
        warning_flags = false;
    }else if(v > 25){
		bat_label = battery_50;
        warning_flags = false;
	}else if(v>10){
		bat_label = battery_25;
        wcscpy(warning_label, L"电池电量低");
        label_color = YELLOW;
        warning_flags = true;
	}else{
		bat_label = battery_0;
        wcscpy(warning_label, L"电池耗尽");
        label_color = RED;
        warning_flags = true;
	}
    if(bat_status == true){
        warning_flags = false;
    }
	
}

void show_bat()
{
//	bat_label = battery_0;
	int k,j;
	for(k=0; k < 50; k++){
		for(j=0; j<20; j++){
			memcpy(fbmem+line_width*k+25*line_width+32,bat_label+k*80*sizeof(char),80*sizeof(char));
		}	
	}	
}

void show_photo()
{
	int j;
	for(j=0; j < 80; j++){
			memcpy(fbmem+line_width*j+(280*line_width+line_width/2-80),gImage_camera+j*79*4*sizeof(char),79*4*sizeof(char));	
	}	
}


inline int clip(int value, int min, int max) {  
    return (value > max ? max : value < min ? min : value);  
}  
 
/**
 *
 */
void testFrambeBuffer(int devfd)
{
	struct fb_var_screeninfo var_info;
	struct fb_fix_screeninfo fix_info;

	DBG("===============\n");

	if (devfd < 0)
		return;

	ioctl(devfd, FBIOGET_FSCREENINFO, &fix_info);
	DBG("smem_len %d\n", fix_info.smem_len);
	DBG("type %d\n", fix_info.type);    //framebuffer type
	DBG("type_aux %d\n", fix_info.type_aux);    //framebuffer type, Interleave for interleaved Planes
	DBG("visual %d\n", fix_info.visual);    //FB_VISUAL_

	DBG("xpanstep %d\n", fix_info.xpanstep);    //zero if no hardware panning
	DBG("ypanstep %d\n", fix_info.ypanstep);    //zero if no hardware panning
	DBG("ywrapstep %d\n", fix_info.ywrapstep);    //zero if no hardware ywrap
	DBG("line_length %d\n", fix_info.line_length);    //
	DBG("mmio_start 0x%x\n", fix_info.mmio_start);    //
	DBG("mmio_len %d\n", fix_info.mmio_len);    //
	DBG("accel %d\n", fix_info.accel);    //



	ioctl(devfd, FBIOGET_VSCREENINFO, &var_info);
	DBG("xres %d\n", var_info.xres);
	DBG("yres %d\n", var_info.yres);
	DBG("xres_virtual %d\n", var_info.xres_virtual);
	DBG("yres_virtual %d\n", var_info.yres_virtual);
	DBG("xoffset %d\n", var_info.xoffset);
	DBG("yoffset %d\n", var_info.yoffset);

	DBG("bits_per_pixel %d\n", var_info.bits_per_pixel)	;
	DBG("grayscale %d\n", var_info.grayscale)	;

	DBG("red s:%d, offset:%d\n", var_info.red.length, var_info.red.offset);
	DBG("green s:%d, offset:%d\n", var_info.green.length, var_info.green.offset);
	DBG("blue s:%d, offset:%d\n", var_info.blue.length, var_info.blue.offset);
	DBG("alpha s:%d, offset:%d\n", var_info.transp.length, var_info.transp.offset);

	DBG("hsync_len %d\n", var_info.hsync_len)	;
	DBG("vsync_len %d\n", var_info.vsync_len)	;
	DBG("sync %d\n", var_info.sync)	;
	DBG("vmode %d\n", var_info.vmode);



	//int line_size = var_info.xres * var_info.bits_per_pixel / 8;


}

void testScreen()
{
	int f;
	f = open("/dev/fb0",O_RDWR);
	if(f>0)
		testFrambeBuffer(f);
	close(f);
	f = -1;

	f = open("/dev/fb1",O_RDWR);
	if(f>0)
		testFrambeBuffer(f);
	close(f);
	f = -1;

	f = open("/dev/fb2",O_RDWR);
	if(f>0)
		testFrambeBuffer(f);
	close(f);
	f = -1;

	f = open("/dev/fb3",O_RDWR);
	if(f>0)
		testFrambeBuffer(f);
	close(f);
	f = -1;

	f = open("/dev/fb4",O_RDWR);
	if(f>0)
		testFrambeBuffer(f);
	close(f);
	f = -1;
}

void saveOneImage(void * p, int size)
{
	FILE * f;
	f=fopen("img.yuyv","wb");
	if(f==NULL)
		return;
	fwrite(p,1,size,f);
	fflush(f);
	fclose(f);

	printf("saved\n");
}


static void process_image(const void * p)
{
	const unsigned char *jpeg_data = p;
	static int recording_count = 0;
	static int recording_flag = 0;
	static int offsetflag = 0;
	int px = 0;
	int screen_col = 0;
	const int screen_colsize = 640;
	const int screen_rowCount = 640;
	const int line_stroke = 480*4;
	int screen_row = 0;
	const int screen_rowsize = 480;  //
	int  fbbase;
        unsigned char *fbp_frame;
        unsigned char *working_col;
        int i;

        if (updateFramebufferFlag==false) {
//            show_time();
            return;
        }
	if(++recording_count>20){
		recording_count = 0;
		recording_flag ^= 1;
	}
        
	offsetflag ^= 1;
        if(offsetflag) {
            fbp_frame = fbp + FRAME_MEMORY_SIZE;
        } else {
            fbp_frame = fbp;
        }

	//TSS();
	jpeg_mem_src(&cinfo, jpeg_data, mjpeg_frame_size);
        jpeg_read_header(&cinfo, TRUE);
        jpeg_start_decompress(&cinfo); 
	while (cinfo.output_scanline < cinfo.output_height) {
                //read one row and rotate 90
                rowp[0] = row_tmp;
		jpeg_read_scanlines(&cinfo, rowp, 1);
                working_col = fbp_frame + (480 - cinfo.output_scanline)*4;
                for(i=0; i < 640;++i) {
			//memcpy(working_col+i*480*4, row_tmp+i*3, 3);
                	working_col[i*480*4]=row_tmp[i*3+2];
                	working_col[i*480*4+1]=row_tmp[i*3+1];
                	working_col[i*480*4+2]=row_tmp[i*3];
                }
        }
        //Data is written. set framebuffer to n/ew location
		// bat label 
		fbmem = fbp_frame;
		update_bat();
		show_bat();
		if(recordingFlag == true && recording_flag == 1){
			show_recording(430,30);
		}
		
		if(photosnapshotFlag == true){
			photosnapshotFlag = false;
			show_photo();
		}
		show_time();
        if(warning_flags == true)
        {
            show_fonts(450,630,warning_label);
        }
	vinfo.yoffset = 640 * offsetflag; //show
	if (ioctl(fbfd, FBIOPAN_DISPLAY, &vinfo) < 0) {
		printf("Error setting FBIOPAN_DISPLAY .\n");
	}
        jpeg_finish_decompress(&cinfo);
        //TSE();
        //must wait for this long in realtime playback. or there will be little triangle flicker 
        //usleep(1000); 
	//DBG("%d us\n",TS_DURATION());

	return ;
}


bool saveOneSnapshot(const void *p)
{
    FILE *fh;
    char filename[50];
    getFilename("/udisk/images/pic", "jpg", filename, sizeof(filename));
    fh = fopen(filename, "wb");
    if(fh==NULL)
        return false;
    fwrite(p,1, mjpeg_frame_size,fh);
    fflush(fh);
    fclose(fh);

    printf("saved\n");
    return true;
}

bool writeToVideo(const void *p)
{
    if (fh_video == NULL) {
        char filename[50];
        getFilename("/udisk/images/video", "mjpeg", filename, sizeof(filename));
        fh_video = fopen(filename, "wb");
        if(fh_video==NULL)
            return false;
    }
    fwrite(p, 1, mjpeg_frame_size/2, fh_video);
    fflush(fh_video);
 /*   recorded_frames ++;
    if(recorded_frames == frames_to_record) {
        finalizeVideoFlag=true;
    }*/
    return true;
}

bool finalizeVideo()
{
    if (fh_video == NULL) {
        //Nothing to do. Video is not recording
        return false;
    }
    fflush(fh_video);
    fclose(fh_video);
    fh_video=NULL;
    return true;
}

void getFilename(char *base, char *ext, char *result, int len)
{
    char filename[40];
    char timestamp[16];
    struct tm *tm;

    time_t now = time(NULL);
    tm = localtime(&now);

    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm);
    snprintf(result, len, "%s_%s.%s", base, timestamp, ext);
}

void set_alpha(void)
{
    memset(fbp,screensize,0);

    int col;
    for (col=0; col < HEIGHT * 4; col+=4) {
        fbp[col+3] = 255;
    }
}

void fill_screen(void)
{
    memset(fbp,screensize,0);

    int col;
    for (col=0; col < HEIGHT * 4; col+=4) {
        fbp[col+0] = 255;
        fbp[col+3] = 255;
    }
}

void init_jpeg(void) 
{
    cinfo.err = jpeg_std_error(&jerr);	
    jpeg_create_decompress(&cinfo);
    cinfo.out_color_space = JCS_EXT_BGR;
}


static int read_frame (void)  
{  
    struct v4l2_buffer buf;  
    unsigned int i;  
    static char p[HEIGHT*HEIGHT*3]={0};
    CLEAR (buf);  
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  
    buf.memory = V4L2_MEMORY_MMAP;  
    if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {  
        switch (errno) {  
            case EAGAIN:  
                return 0;  
            case EIO:      
            default:  
                errno_exit ("VIDIOC_DQBUF");  
        }  
    }

    if(photosnapshotFlag==true)
    {
        saveOneSnapshot(buffers[buf.index].start);
        //photosnapshotFlag=false;  //wait for next capture event
    }
    if(recordingFlag==true) 
    {
        writeToVideo(buffers[buf.index].start);
    }
    process_image(buffers[buf.index].start);

    memset(buffers[buf.index].start, 0x0, buffers[buf.index].length);
    if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))  
        errno_exit ("VIDIOC_QBUF");  

    return 1;  
}  

#define STDIN  0

void playbackVideo(const char *file)
{
    FILE *fh;
    unsigned char *mem;
    int stop_flags = 0;
    char buf[10] = {0};
    if (file == NULL) {
        return;
    }
    fh = fopen(file, "rb");
    if (fh == NULL) {
        return;
    }
    mem = malloc(mjpeg_frame_size/2);
    if (mem == NULL) {
        fclose(fh);
        return;
    }
	for(;;){
        int fdmax = STDIN;
        fd_set fds;  
        int r;
        struct timeval tv;  
		FD_SET (STDIN, &fds);
		//FD_SET (fh, &fds);
        tv.tv_sec = 0;  
        tv.tv_usec = 30000;
        r = select (fdmax + 1, &fds, NULL, NULL, &tv);
        if (-1 == r) {
            if (EINTR == errno) {
                continue;
            }
            errno_exit ("select");
        }
        if (0 == r) {
            //fprintf (stderr, "select timeout\n");
            //exit (EXIT_FAILURE);
			if(stop_flags == 1)
				continue ;				
			
			if (fread(mem, mjpeg_frame_size/2, 1, fh) == 1) {	//get a frame form file
				process_image(mem);
				//usleep(30000);
			}else{
				break;
			}            
            continue;
        }		

       if(FD_ISSET(STDIN, &fds)) {
           if(! fgets(buf, sizeof(buf), stdin)) {
               if(ferror(stdin)) {
                   perror("stdin");
               }
           }	
		   if(strncmp(buf, "p", 1)==0) {  //p for pause
				stop_flags = 1;
		    }
			
			if(strncmp(buf, "r", 1)==0) {  //r for resume
				 stop_flags = 0;
			 }
			if(strncmp(buf, "s", 1)==0) {  //s for stop
				break;
			 }

        }


	}
    free(mem);
    fclose(fh);
}

static void run (void)  
{  
    unsigned int count;  
    int frames;  
    frames = 30 * time_in_sec_capture;  
    int reportElapseFlag = 1;
    struct input_event event;
    struct input_event event_old;
    char buf[10];

    DBG("RUN...\n");

    while (terminateFlag == false) {  
        for (;;) {  
        	int fdmax;
            fd_set fds;  
            struct timeval tv;  
            int r;  
            FD_ZERO (&fds);  
            FD_SET (STDIN, &fds);
            FD_SET (fd, &fds);  
            FD_SET (key_fd, &fds);
            tv.tv_sec = 1;  
            tv.tv_usec = 0;
            if(fd > key_fd) {
                fdmax = fd;
            } else {
                fdmax = key_fd;
            }
            r = select (fdmax + 1, &fds, NULL, NULL, &tv);
            if (-1 == r) {
            //show_time();
                if (EINTR == errno) {
                    continue;
                }
                errno_exit ("select");
            }
            if (0 == r) {
                fprintf (stderr, "select timeout\n");
               // show_time();
                //exit (EXIT_FAILURE);
                continue;
            }

            /*if(FD_ISSET(key_fd, &fds)){
                read(key_fd, &event, sizeof(struct input_event));  
                if (event.type == EV_KEY)  {  		  
                    if(event.value == KEY_DOWN){
                        event_old = event;
                    } else {
                        if((event.code == 0x1)&&(event.code == event_old.code)){		// photo key
                            if((event.time.tv_sec - event_old.time.tv_sec) > 1) {
                                printf("key number : %x long press\n", event.code);
                                if(recordingFlag == false) {
                                    recordingFlag = true;
                                } else {
                                    finalizeVideoFlag = true;
                                }
                            } else {
                                printf("key number : %x short press\n", event.code);
                                if(recordingFlag == true) {
                                    finalizeVideoFlag = true;
                                } else {
                                    photosnapshotFlag = true;
                                }  
                            }
                        }
                        if(event.code == 0x60){		//menu key
                        }

                        if(event.code == 0x67){		// up key
                        }

                        if(event.code == 0x6c){		//down key
                        }
                    }
                    //printf("key number is %x\n",event.code);  
                    //printf("key :%c.\n",event.code);
                }
           }*/
           if(FD_ISSET(STDIN, &fds)) {
               if(! fgets(buf, sizeof(buf), stdin)) {
                   if(ferror(stdin)) {
                       perror("stdin");
                   }
               }
               if(strncmp(buf, "p", 1)==0) {  //p for pause
                   //temporary close framebuffer, let QT do the job
                   updateFramebufferFlag=  false;
               }
               if(strncmp(buf, "r", 1)==0) {  //r for resume
                   //resume display
                   updateFramebufferFlag = true;
               }
               if(strncmp(buf, "s", 1)==0) {  //s for stop (application) 
                   //resume display
                   terminateFlag = true;
               }
               if(strncmp(buf, "R", 1)==0) {
					recordingFlag = true;
               }
               if(strncmp(buf, "c", 1)==0) {
					finalizeVideoFlag = true;
               }
               if(strncmp(buf, "P", 1)==0) {
					photosnapshotFlag = true;
               }
               
           }
           if(finalizeVideoFlag==true) // set video recoder exit , write final frame to video
           {
               finalizeVideo();
               finalizeVideoFlag = false; 
               recordingFlag = false; //since video is finalized, should clear recordingFlag too.
           }

           int ret;
           //gettimeofday(&start_time, NULL);
           ret = read_frame ();		//if not process image only cost less than 1ms
           //gettimeofday(&end_time, NULL);  //90ms, 11 frames /s

           exit;

           if(reportElapseFlag)
           {
               //DBG("%d us\n", TS_DURATION() );
               //flag = 0;
           }

           if (ret)
               break;

        }  
    }  
}  


void run2()
{
    static int offsetflag = 0;
    int fb_virt_offset;
    int fbbase;
    DBG("RUN2...\n");
    int c = 0;
    while (1) {
        vinfo.yoffset = 640 * offsetflag; //show
        if (ioctl(fbfd, FBIOPAN_DISPLAY, &vinfo) < 0) {
            printf("Error setting FBIOPAN_DISPLAY .\n");
        }

        offsetflag ^= 1;
        fb_virt_offset = 640 * 480 * 4 * offsetflag;
        c=255;
        int i;
        for (i = 0; i < HEIGHT * WIDTH * 4; i += 4) {
            fbbase = i + fb_virt_offset;
            if(offsetflag)
            {
                fbp[fbbase + 0] = c; //b
                fbp[fbbase + 1] = 0;
                fbp[fbbase + 2] = 0;
            }
            else
            {
                fbp[fbbase + 0] = 0; //b
                fbp[fbbase + 1] = 0;
                fbp[fbbase + 2] = 0;
            }
            fbp[fbbase + 3] = 255;
        }
        usleep(10 * 1000); //20 frame
    }
}

static void stop_capturing (void)  
{  
    enum v4l2_buf_type type;  
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  
    if (-1 == xioctl (fd, VIDIOC_STREAMOFF, &type))  
        errno_exit ("VIDIOC_STREAMOFF");  
}    

static void start_capturing (void)  
{  
    unsigned int i;  
    enum v4l2_buf_type type;  
    for (i = 0; i < n_buffers; ++i) {  
        struct v4l2_buffer buf;  
        CLEAR (buf);  
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  
        buf.memory = V4L2_MEMORY_MMAP;  
        buf.index = i;  
        if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))  
            errno_exit ("VIDIOC_QBUF");  
    }  
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  
    if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))  
        errno_exit ("VIDIOC_STREAMON");  
}  

static void uninit_device (void)  
{  
    unsigned int i;  
    for (i = 0; i < n_buffers; ++i)  
        if (-1 == munmap (buffers[i].start, buffers[i].length))  
            errno_exit ("munmap");  

    free (buffers);  
}  

static void init_mmap (void)  
{  
    struct v4l2_requestbuffers req;  
    //mmap framebuffer  

    CLEAR (req);  
    req.count = 4;  
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  
    req.memory = V4L2_MEMORY_MMAP;  

    if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {  
        if (EINVAL == errno) {  
            fprintf (stderr, "%s does not support memory mapping\n", dev_name);  
            exit (EXIT_FAILURE);  
        } else {  
            errno_exit ("VIDIOC_REQBUFS");  
        }  
    }  

    if (req.count < 4) {    //if (req.count < 2)  
        fprintf (stderr, "Insufficient buffer memory on %s\n",dev_name);  
        exit (EXIT_FAILURE);  
    }  
    buffers = calloc (req.count, sizeof (*buffers));  
    if (!buffers) {  
        fprintf (stderr, "Out of memory\n");  
        exit (EXIT_FAILURE);  
    }  
    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {  
        struct v4l2_buffer buf;  
        CLEAR (buf);  
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  
        buf.memory = V4L2_MEMORY_MMAP;  
        buf.index = n_buffers;  
        if (-1 == xioctl (fd, VIDIOC_QUERYBUF, &buf))  
            errno_exit ("VIDIOC_QUERYBUF");  
        buffers[n_buffers].length = buf.length;
        mjpeg_frame_size = buf.length;
        buffers[n_buffers].start =mmap (NULL,buf.length,PROT_READ | PROT_WRITE ,MAP_SHARED,fd, buf.m.offset);  
        if (MAP_FAILED == buffers[n_buffers].start)  
            errno_exit ("mmap");  
    }  
}  

static void testDeviceCap()
{
    struct v4l2_fmtdesc fmt;
    int ret;

    INFO("test device accepted format\n");

    memset(&fmt, 0, sizeof(fmt));
    fmt.index = 0;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while ((ret = ioctl(fd, VIDIOC_ENUM_FMT, &fmt)) == 0) {
        fmt.index++;
        printf("{ pixelformat = ''%c%c%c%c'', description = ''%s'' }\n", fmt.pixelformat & 0xFF,
                (fmt.pixelformat >> 8) & 0xFF, (fmt.pixelformat >> 16) & 0xFF, (fmt.pixelformat >> 24) & 0xFF,
                fmt.description);
    }
}
/**
 *
 */
static void init_device (void)  
{  
    struct v4l2_capability cap;  
    struct v4l2_cropcap cropcap;  
    struct v4l2_crop crop;  
    struct v4l2_format fmt;  
    unsigned int min;  
	
    if (-1 == xioctl (fd, VIDIOC_QUERYCAP, &cap)) {  
        if (EINVAL == errno) {  
            fprintf (stderr, "%s is no V4L2 device\n",dev_name);  
            exit (EXIT_FAILURE);  
        } else {  
            errno_exit ("VIDIOC_QUERYCAP");  
        }  
    }  

    testDeviceCap();

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {  
        fprintf (stderr, "%s is no video capture device\n",dev_name);  
        exit (EXIT_FAILURE);  
    }  

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {  
       fprintf (stderr, "%s does not support streaming i/o\n",dev_name);  
       exit (EXIT_FAILURE);  
    }  
    CLEAR (cropcap);  
    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  
    if (0 == xioctl (fd, VIDIOC_CROPCAP, &cropcap)) {  
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  
        crop.c = cropcap.defrect;  
        if (-1 == xioctl (fd, VIDIOC_S_CROP, &crop)) {  
            switch (errno) {  
                case EINVAL:      
                    break;  
                default:  
                    break;  
            }  
        }  
    }else {     }  
    CLEAR (fmt);  
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;  //V4L2_PIX_FMT_YUV422P, V4L2_PIX_FMT_YUYV
    fmt.fmt.pix.field = V4L2_FIELD_NONE;  
    if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt))  
        errno_exit ("VIDIOC_S_FMT");

    struct v4l2_format fmt_chk;
    fmt_chk.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl (fd, VIDIOC_G_FMT, &fmt_chk))
        errno_exit ("VIDIOC_G_FMT");

    DBG("type 0x%x\n", fmt_chk.type);
    DBG("width %d\n", fmt_chk.fmt.pix.width);
    DBG("height %d\n", fmt_chk.fmt.pix.height);
    DBG("fmt 0x%x\n", fmt_chk.fmt.pix.pixelformat);
    DBG("field 0x%x\n", fmt_chk.fmt.pix.field);

    init_mmap ();  
}
  
static void close_device (void)  
{  
    if (-1 == close (fd))  
        errno_exit ("close");  
    fd = -1;  
    close(fbfd);  
}
  
/*
* open fb dev & key event dev
*/
void open_fb(void)			
{
    //open framebuffer  
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd==-1) {  
        printf("Error: cannot open framebuffer device.\n");  
        exit (EXIT_FAILURE);  
    }  	
    // Get fixed screen information  
    if (-1==xioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {  
        printf("Error reading fixed information.\n");  
        exit (EXIT_FAILURE);  
    }  
    // Get variable screen information  
    if (-1==xioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {  
        printf("Error reading variable information.\n");  
        exit (EXIT_FAILURE);  
    }  

    line_width = vinfo.xres * vinfo.bits_per_pixel / 8;
    pixel_width = vinfo.bits_per_pixel / 8;
    
    //screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
    screensize = vinfo.xres_virtual * vinfo.yres_virtual * vinfo.bits_per_pixel / 8;

    DBG("screen %d x %d y%d bit/px %d\n", screensize, vinfo.xres_virtual, vinfo.yres_virtual,
    		vinfo.bits_per_pixel);

    fbp = (char *)mmap(NULL,screensize,PROT_READ | PROT_WRITE,MAP_SHARED ,fbfd, 0);  
    fbmem = fbp;
    if ((int)fbp == -1) {  
        printf("Error: failed to map framebuffer device to memory.\n");  
        exit (EXIT_FAILURE) ;  
    }  
    //memset(fbp, 0, screensize);  
	
	// key event
	key_fd = open("/dev/input/event0", O_RDONLY);
	if(-1 == key_fd){
		fprintf(stderr, "Cannot open event0 \n");
		exit(EXIT_FAILURE);	
	}
}
void close_fb(void)
{
    if (-1 == munmap(fbp, screensize)) {  
        printf(" Error: framebuffer device munmap() failed.\n");  
        exit (EXIT_FAILURE) ;  
    }       
	close(fbfd);	
	close(key_fd);
}

/**
 * @brief: open framebuffer and camera device
 */
static void open_device (void)  
{  
    struct stat st;    
    if (-1 == stat (dev_name, &st)) {  
        fprintf (stderr, "Cannot identify '%s': %d, %s\n",dev_name, errno, strerror (errno));  
        exit (EXIT_FAILURE);  
    }  

    if (!S_ISCHR (st.st_mode)) {  
        fprintf (stderr, "%s is no device\n", dev_name);  
        exit (EXIT_FAILURE);  
    }  


    //open camera  
    fd = open (dev_name, O_RDWR| O_NONBLOCK, 0);  
    if (-1 == fd) {  
        fprintf (stderr, "Cannot open '%s': %d, %s\n",dev_name, errno, strerror (errno));  
        exit (EXIT_FAILURE);  
    }  

	open_fb();
    queryDeviceControlCapability(fd);
}  

static void usage (FILE * fp,int argc,char ** argv)  
{  
    fprintf (fp,  
            "Usage: %s [options]\n\n"  
            "Options:\n"  
            "-d | --device name Video device name [/dev/video]\n"  
            "-h | --help Print this message\n"  
            "-t | --how many frames to record\n" 
            "-s | --snapshot capture 1st frame\n"
            "-r | --record record to mjpeg video\n"
            "",  
            argv[0]);  
}  

static const char short_options [] = "d:ht:srp:";  

static const struct option long_options [] = {  
    { "device", required_argument, NULL, 'd' },  
    { "help", no_argument, NULL, 'h' },  
    { "time", required_argument, NULL, 't' },  
    { "snapshot", no_argument, NULL, 's' },
    { "record", no_argument, NULL, 'r' },
    { "play", required_argument, NULL, 'p' },
    { 0, 0, 0, 0 }  
};  



/**
 *
 * @param argc
 * @param argv
 * @return
 */
int main (int argc,char ** argv)  
{  
    dev_name = "/dev/video3";
    printf("%s %s\n",__DATE__,__TIME__);

    for (;;)    
    {  
        int index;  
        int c;  
        c = getopt_long (argc, argv,short_options, long_options,&index);  
        if (-1 == c)  
            break;  

        switch (c) {  
            case 0:  
                break;  
            case 'd':  
                dev_name = optarg;  
                break;  
            case 'h':  
                usage (stdout, argc, argv);  
                exit (EXIT_SUCCESS);  
            case 't':  
                frames_to_record = atoi(optarg);  
                break;
			case 'p':  
                playbackFlag = true;
                file_to_play = optarg;  
                break;
            case 'r':
                recordingFlag = true;
                break;            
            case 's':
                photosnapshotFlag = true;
                break;
            default:  
                usage (stderr, argc, argv);  
                exit (EXIT_FAILURE);  
        }  
    }  
    init_jpeg();
	open_fb();
    freetype_init();
	if(playbackFlag==true) {
	playbackVideo(file_to_play);
	playbackFlag=false; 
	return 0;
	}
	
    open_device();  
    init_device();
    testFrambeBuffer(fbfd);

    //set_alpha();
    //show_fonts(240, 320,  chinese_char);
    start_capturing ();  
    run();

    stop_capturing ();  
    uninit_device ();  
    close_device (); 
	close_fb();
    exit (EXIT_SUCCESS);  
    return 0;  
}  
