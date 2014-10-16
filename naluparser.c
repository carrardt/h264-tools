#include <stdio.h>
#include <malloc.h>
#include <stdint.h>

static const unsigned char AccessUnitDelimiter[] = { 0x00, 0x00, 0x00, 0x01, 0x09, 0x10 };
static const unsigned char StartCode[] = {0x00, 0x00, 0x00, 0x01};

size_t readInput(unsigned char* buf, size_t n)
{
	size_t count=0;
	size_t p=0;
	while(count<n)
	{
		p = read(0,buf+count,n-count);
		if( p < 0 ) { return -1; }
		if( p == 0 ) { return count; }
		count += p;
	}
	return count;
}

int parseAnnexB(int writeOutput, int naluStartCode, size_t maxNALUs, unsigned char lbuf[4],int verbose)
{
	size_t naluLen=0;
	size_t maxNaluSize = 1024*1024;
	size_t naluCount = 0;
	size_t naluTotalBytes = 0;
	int i;
	int zeros=0;
	unsigned char* buf = (unsigned char*) malloc(maxNaluSize);

	while( naluCount<maxNALUs && readInput(buf+naluLen,1) == 1 )
	{
		++naluLen;
		if( buf[naluLen-1]==0x00 ) ++zeros;
		else if( buf[naluLen-1]==0x01 && zeros==3 )
		{
			naluLen -= 4;
			lbuf[0] = (naluLen>>24) & 0xFF;
			lbuf[1] = (naluLen>>16) & 0xFF;
			lbuf[2] = (naluLen>>8) & 0xFF;
			lbuf[3] = naluLen & 0xFF;
			if( writeOutput && naluLen>0)
			{
				if( naluStartCode ) { write(1,StartCode,4); }
				else { write(1,lbuf,4); }
				write(1,buf,naluLen);
			}
			++naluCount;
			naluTotalBytes += naluLen;
			if( naluCount % 256 == 0 || verbose >= 2 )
			{
				fprintf(stderr,"NALUs: %ld\tMb: %ld, last NALU size %ld                       \r"
					,naluCount,naluTotalBytes/(1024*1024),naluLen);
			}
			naluLen=0;
			zeros=0;
		}
		else { zeros=0; }

		if( naluLen >= maxNaluSize )
		{
			fprintf(stderr,"realloc NALU buffer to %ld bytes\n",naluLen);
			maxNaluSize = naluLen*2;
			buf = (unsigned char*) realloc(buf,maxNaluSize);
		}
	}
	if( writeOutput && naluLen>0 )
	{
		if( naluStartCode ) { write(1,StartCode,4); }
		else { write(1,lbuf,4); }
		write(1,buf,naluLen);
	}

	fprintf(stderr,"\nNumber of NALUs : %ld\n",naluCount);
	fprintf(stderr,"Total NALU bytes : %ld\n",naluTotalBytes);

	free( buf );
	return 1;
}

// writeOutput: 0 no output, 1 write to standard output
int parseMKVH264(int writeOutput, int naluStartCode, size_t maxNALUs, unsigned char lbuf[4],int verbose)
{
	size_t naluLen=0;
	size_t maxNaluSize = 1024*1024*4;
	size_t naluCount = 0;
	size_t naluTotalBytes = 0;
	int i;
	unsigned char* buf = (unsigned char*) malloc(maxNaluSize);

	do 
	{
		naluLen = ( ((unsigned int)lbuf[0]) << 24 )
			| ( ((unsigned int)lbuf[1]) << 16 ) 
			| ( ((unsigned int)lbuf[2]) << 8 ) 
			| ((unsigned int)lbuf[3]) ;
		//printf("@%016lX: %02X %02X %02X %02X => %ld\n",naluTotalBytes+naluCount*4,buf[0],buf[1],buf[2],buf[3],naluLen);

		if( naluLen > maxNaluSize )
		{
			fprintf(stderr,"realloc NALU buffer to %ld bytes\n",naluLen);
			maxNaluSize = naluLen;
			buf = (unsigned char*) realloc(buf,maxNaluSize);
		}

		size_t bytesRead = readInput(buf,naluLen);
		if( bytesRead < 0 )
		{
			fprintf(stderr,"I/O Error, aborting\n");
			return 0;
		}
		else if( bytesRead < naluLen )
		{
			fprintf(stderr,"Premature end of file, aborting\n");
			return 0;
		}
		if( writeOutput && naluLen>0 )
		{
			if( naluStartCode ) { write(1,StartCode,4); }
			else { write(1,lbuf,4); }
			write(1,buf,naluLen);
		}
		++naluCount;
		naluTotalBytes += naluLen;
		if( naluCount % 256 == 0 || verbose >= 2 )
		{
			fprintf(stderr,"NALUs: %ld\tMb: %ld, last NALU size %ld                       \r"
				,naluCount,naluTotalBytes/(1024*1024),naluLen);
		}
	} while( naluCount<maxNALUs && readInput(lbuf,4) == 4 );

	fprintf(stderr,"\nNumber of NALUs : %ld\n",naluCount);
	fprintf(stderr,"Total NALU bytes : %ld\n",naluTotalBytes);

	free( buf );
	return 1;
}

int main(int argc, char* argv[])
{
	int writeResult=1, startcode=1, verbose=0;
	int i,ok;
	unsigned char buf[4];
	size_t maxNALUs = 1ull<<62;
	for(i=1;i<argc;i++)
	{
		if( strcmp(argv[i],"-mkv") == 0 ) startcode = 0;
		else if( strcmp(argv[i],"-annexb") == 0 ) startcode = 1;		
		else if( strcmp(argv[i],"-stat") == 0 ) writeResult = 0;
		else if( strcmp(argv[i],"-nalucount") == 0 ) { ++i; maxNALUs = atoi(argv[i]); }
		else if( strcmp(argv[i],"-v") == 0 ) { ++verbose; }
	}

	fprintf(stderr,"Write=%d, StartCodes=%d, NALUCount=%ld\n",writeResult,startcode,maxNALUs);

	if( readInput(buf,4) != 4 ) return 1;
	if( buf[0]==0x00 && buf[1]==0x00 && buf[2]==0x00 && buf[3]==0x01 )
	{
		fprintf(stderr,"Reading Annex B input\n");
		ok = parseAnnexB(writeResult,startcode,maxNALUs,buf,verbose);
	}
	else
	{
		fprintf(stderr,"Reading MKV's raw h264 format\n");
		ok = parseMKVH264(writeResult,startcode,maxNALUs,buf,verbose);
	}

	return (ok==0);
}

