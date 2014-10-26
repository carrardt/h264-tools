#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

void make_sbs(int w,int h, char* l, char* r, char* o)
{
	int row;
	// Y
	for(row=0;row<h;row++)
	{
		memcpy(o,l,w); o+=w; l+=w;
		memcpy(o,r,w); o+=w; r+=w;
	}
	// U
	for(row=0;row<(h/2);row++)
	{
		memcpy(o,l,w/2); o+=w/2; l+=w/2;
		memcpy(o,r,w/2); o+=w/2; r+=w/2;
	}
	// V
	for(row=0;row<(h/2);row++)
	{
		memcpy(o,l,w/2); o+=w/2; l+=w/2;
		memcpy(o,r,w/2); o+=w/2; r+=w/2;
	}
}

void make_tab(int w,int h, char* l, char* r, char* o)
{
	int row;
	// Y
	for(row=0;row<h;row++) { memcpy(o,l,w); o+=w; l+=w; }
	for(row=0;row<h;row++) { memcpy(o,r,w); o+=w; r+=w; }
	// U
	for(row=0;row<(h/2);row++) { memcpy(o,l,w/2); o+=w/2; l+=w/2; }
	for(row=0;row<(h/2);row++) { memcpy(o,r,w/2); o+=w/2; r+=w/2; }
	// V
	for(row=0;row<(h/2);row++) { memcpy(o,l,w/2); o+=w/2; l+=w/2; }
	for(row=0;row<(h/2);row++) { memcpy(o,r,w/2); o+=w/2; r+=w/2; }
}


int main(int argc, char* argv[])
{
	void(*assemble)(int,int,char*,char*,char*) = make_sbs;
	int fd_in[2] = { -1, -1 } ;
	int fd_out = 1;
	int i;

	size_t width = 1920;
	size_t height = 1080;
	size_t imgbytes = width*height + (width*height)/2; // for YUV420p
	size_t bufimages = 24;
	ssize_t nbytes;

	int imgtoread[2] = {0,0};
	int imgtowrite = 0;
	int* imgp[2] = { NULL, NULL }; // number of bytes read to image. if this = imgbytes, image is complete
	char* buffer[2] = {NULL,NULL};
	char* sbsimage = NULL;

        for(i=1;i<argc;i++)
        {
                if( strcmp(argv[i],"-w") == 0 )
		{
			++i;
			width = atoi(argv[i]);
		}
                if( strcmp(argv[i],"-h") == 0 )
		{
			++i;
			height = atoi(argv[i]);
		}
                if( strcmp(argv[i],"-n") == 0 )
		{
			++i;
			bufimages = atoi(argv[i]);
		}
                else if( strcmp(argv[i],"-l") == 0 )
		{
			++i;
			fd_in[0]=open(argv[i],O_RDONLY|O_NONBLOCK);
			if( fd_in[0]<0 ) {fprintf(stderr,"Can't open Left input\n"); return 1; }
		}
                else if( strcmp(argv[i],"-r") == 0 )
		{
			++i;
			fd_in[1]=open(argv[i],O_RDONLY|O_NONBLOCK);
			if( fd_in[1]<0 ) {fprintf(stderr,"Can't open Right input\n"); return 1; }
		}
                else if( strcmp(argv[i],"-o") == 0 )
		{
			++i;
			fd_out = open(argv[i],O_WRONLY);
			if( fd_out<0 ) {fprintf(stderr,"Can't open output\n"); return 1; }
		}
                else if( strcmp(argv[i],"-sbs") == 0 )
		{
			assemble = make_sbs;
		}
                else if( strcmp(argv[i],"-tab") == 0 )
		{
			assemble = make_tab;
		}
        }

	imgbytes = width*height + (width*height)/2;
	buffer[0] = (char*) malloc(bufimages*imgbytes);
	buffer[1] = (char*) malloc(bufimages*imgbytes);
	imgp[0] = (int*) calloc( bufimages, sizeof(int) );
	imgp[1] = (int*) calloc( bufimages, sizeof(int) );
	sbsimage = (char*) calloc(2, imgbytes);
	//fprintf(stderr,"buffer size is %ldx %ldx%ld\n",bufimages,width,height);

	do
	{
	    for(i=0;i<2;i++)
	    {
		nbytes = imgbytes - imgp[i][imgtoread[i]];
		if( nbytes > 0 )
		{
		    if(fd_in[i]>=0){ nbytes = read( fd_in[i], buffer[i] + imgtoread[i]*imgbytes + imgp[i][imgtoread[i]] , nbytes ); }
		    else { nbytes = 0; }
		    if( nbytes == 0 )
		    {
			//fprintf(stderr,"\n\nEnd of input #%d\n\n",i);
			close(fd_in[i]);
			fd_in[i] = -1;
		    }
		}
		if( nbytes < 0 ) nbytes = 0;
		imgp[i][imgtoread[i]] += nbytes;
		if( imgp[i][imgtoread[i]] == imgbytes )
		{
		    int ni = ( imgtoread[i] + 1 ) % bufimages;
		    if( imgp[i][ni] == 0 )
		    {
			//fprintf(stderr,"Got %d / %d\n", i, ni);
			imgtoread[i] = ni;
		    }
		}
	    }
	    if( imgp[0][imgtowrite]==imgbytes && imgp[1][imgtowrite]==imgbytes )
	    {
		//fprintf(stderr,"Write %d\n",imgtowrite);
		(*assemble) ( width, height, buffer[0]+imgtowrite*imgbytes, buffer[1]+imgtowrite*imgbytes, sbsimage );
		nbytes = write(fd_out,sbsimage,2*imgbytes);
		if( nbytes != (2*imgbytes) )
		{
			fprintf(stderr,"Write Error: %ld != %ld\n",nbytes,2*imgbytes);
			return 1;
		}
		imgp[0][imgtowrite] = 0;
		imgp[1][imgtowrite] = 0;
		imgtowrite = ( imgtowrite + 1 ) % bufimages;
	    }
	}
	while(fd_in[0]>=0 && fd_in[1]>=0);

	return 0;
}

