#include <stdio.h>
#include <malloc.h>
#include <stdint.h>
#include <string.h>

enum  	NAL_unit_type {
  UNKNOWN = 0, SLICE = 1, SLICE_DPA = 2, SLICE_DPB = 3,
  SLICE_DPC = 4, SLICE_IDR = 5, SEI = 6, SPS = 7,
  PPS = 8, AU_DELIMITER = 9, END_SEQUENCE = 10, END_STREAM = 11,
  FILLER_DATA = 12, SPS_EXT = 13, NALU_prefix = 14, SPS_subset = 15,
  AUXILIARY_SLICE = 19, SLICE_EXTENSION = 20
};
 
enum  	SEI_type { SEI_TYPE_PIC_TIMING = 1, SEI_FILLER_PAYLOAD = 3, SEI_TYPE_USER_DATA_UNREGISTERED = 5, SEI_TYPE_RECOVERY_POINT = 6 };
 
enum  	SLICE_type {
  SLICE_P = 0, SLICE_B = 1, SLICE_I = 2, SLICE_SP = 3,
  SLICE_SI = 4, SLICE_P_a = 5, SLICE_B_a = 6, SLICE_I_a = 7,
  SLICE_SP_a = 8, SLICE_SI_a = 9, SLICE_UNDEF = 10
};
 
enum  	frame_type { FRAME = 'F', FIELD_TOP = 'T', FIELD_BOTTOM = 'B' };

//AVC Profile IDC definitions
typedef enum {
  FREXT_CAVLC444 = 44,       //!< YUV 4:4:4/14 "CAVLC 4:4:4"
  BASELINE       = 66,       //!< YUV 4:2:0/8  "Baseline"
  MAIN           = 77,       //!< YUV 4:2:0/8  "Main"
  EXTENDED       = 88,       //!< YUV 4:2:0/8  "Extended"
  FREXT_HP       = 100,      //!< YUV 4:2:0/8  "High"
  FREXT_Hi10P    = 110,      //!< YUV 4:2:0/10 "High 10"
  FREXT_Hi422    = 122,      //!< YUV 4:2:2/10 "High 4:2:2"
  FREXT_Hi444    = 244,      //!< YUV 4:4:4/14 "High 4:4:4"
  MVC_HIGH       = 118,      //!< YUV 4:2:0/8  "Multiview High"
  STEREO_HIGH    = 128       //!< YUV 4:2:0/8  "Stereo High"
} ProfileIDC;


typedef unsigned char byte;

static const unsigned char AccessUnitDelimiterSC[6] = { 0x00, 0x00, 0x00, 0x01, 0x09, 0x10 };
static const unsigned char AccessUnitDelimiterLen[6] = { 0x00, 0x00, 0x00, 0x02, 0x09, 0x10 };
static const unsigned char StartCode[4] = {0x00, 0x00, 0x00, 0x01};

#define IOBUF_SIZE 4096

struct ParserCtx
{
	unsigned char* buf; 
	char iobuf[IOBUF_SIZE];
	size_t iobufpos,iobufsize;
	size_t typeStats[32];
	size_t naluLen, maxNaluSize, naluCount, maxNALUs, naluTotalBytes;
	int naluType,zeros;
	int insertAUD,verbose,writeOutput,naluStartCode,briefStats;
	int sps_id_range[2];
	int profile_idc_range[2];
	int num_ref_frames_range[2];
	unsigned char sc[4];
};

static inline size_t readInput(struct ParserCtx* ctx, unsigned char* buf, size_t n)
{
	size_t count=0;
	size_t p=0;

	size_t nCopyFromIOBuf = ctx->iobufsize - ctx->iobufpos;

	if( nCopyFromIOBuf == 0 )
	{
		ctx->iobufpos = 0;
		p = read(0,ctx->iobuf,IOBUF_SIZE);
		if( p < 0 ) return -1;
		ctx->iobufsize = p;
		nCopyFromIOBuf = ctx->iobufsize - ctx->iobufpos;
	}

	if( nCopyFromIOBuf > n ) nCopyFromIOBuf = n;
	memcpy(buf,ctx->iobuf+ctx->iobufpos,nCopyFromIOBuf);
	count += nCopyFromIOBuf;
	ctx->iobufpos += nCopyFromIOBuf;

	while(count<n)
	{
		p = read(0,buf+count,n-count);
		if( p < 0 ) { return -1; }
		if( p == 0 ) { return count; }
		count += p;
	}
	return count;
}

struct ParserCtx* allocContext()
{
	struct ParserCtx* ctx = (struct ParserCtx*)calloc(1,sizeof(struct ParserCtx));
	ctx->maxNALUs = -1;
	ctx->writeOutput = 1;
	ctx->naluStartCode = 1;
	ctx->maxNaluSize = 1<<20;
	ctx->sps_id_range[0]=255;
	ctx->sps_id_range[1]=-1;
	ctx->profile_idc_range[0]=255;
	ctx->profile_idc_range[1]=-1;
	ctx->num_ref_frames_range[0]=255;
	ctx->num_ref_frames_range[1]=-1;
	ctx->buf = (unsigned char*) calloc(1,ctx->maxNaluSize);
	return ctx;
}

void freeContext(struct ParserCtx* ctx)
{
	if(ctx==0) return;
	if(ctx->buf!=NULL) free(ctx->buf);
	free(ctx); 
}


static inline int linfo_ue(int len, int info)
{
  //assert ((len >> 1) < 32);
  return (int) (((unsigned int) 1 << (len >> 1)) + (unsigned int) (info) - 1);
}

static inline int GetVLCSymbol (byte buffer[],int totbitoffset,int *info, int bytecount)
{
  long byteoffset = (totbitoffset >> 3);         // byte from start of buffer
  int  bitoffset  = (7 - (totbitoffset & 0x07)); // bit from start of byte
  int  bitcounter = 1;
  int  len        = 0;
  byte *cur_byte  = &(buffer[byteoffset]);
  int  ctr_bit    = ((*cur_byte) >> (bitoffset)) & 0x01;  // control bit for current bit posision

  while (ctr_bit == 0)
  {                 // find leading 1 bit
    len++;
    bitcounter++;
    bitoffset--;
    bitoffset &= 0x07;
    cur_byte  += (bitoffset == 7);
    byteoffset+= (bitoffset == 7);
    ctr_bit    = ((*cur_byte) >> (bitoffset)) & 0x01;
  }

  if (byteoffset + ((len + 7) >> 3) > bytecount)
    return -1;
  else
  {
    // make infoword
    int inf = 0;                          // shortest possible code is 1, then info is always 0    

    while (len--)
    {
      bitoffset --;
      bitoffset &= 0x07;
      cur_byte  += (bitoffset == 7);
      bitcounter++;
      inf <<= 1;
      inf |= ((*cur_byte) >> (bitoffset)) & 0x01;
    }

    *info = inf;
    return bitcounter;           // return absolute offset in bit from start of frame
  }
}

#define INSERT_VALUE_TO_RANGE(x,r) do { if(x<ctx->r[0]) ctx->r[0]=x; if(x>ctx->r[1]) ctx->r[1]=x; }while(0)

#define NALU_STATS(ctx) do { \
++ ctx->typeStats[ctx->naluType&31]; \
if( ctx->naluType == SPS ) { \
	int bitoffset=32,len=0,info=0,sps_id=-1,profile_idc=-1,num_ref_frames=-1, vui_prameters_present_flag=-1; \
	profile_idc = ctx->buf[1]; \
	INSERT_VALUE_TO_RANGE(profile_idc,profile_idc_range); \
	bitoffset += ( len = GetVLCSymbol(ctx->buf,bitoffset,&info,ctx->naluLen-(bitoffset/8)) ); \
	sps_id = linfo_ue(len,info); \
	INSERT_VALUE_TO_RANGE(sps_id,sps_id_range); \
	bitoffset=32+len; \
	bitoffset += ( len = GetVLCSymbol(ctx->buf,bitoffset,&info,ctx->naluLen-(bitoffset/8)) ); \
	bitoffset += ( len = GetVLCSymbol(ctx->buf,bitoffset,&info,ctx->naluLen-(bitoffset/8)) ); \
	bitoffset += ( len = GetVLCSymbol(ctx->buf,bitoffset,&info,ctx->naluLen-(bitoffset/8)) ); \
	bitoffset += ( len = GetVLCSymbol(ctx->buf,bitoffset,&info,ctx->naluLen-(bitoffset/8)) ); \
	num_ref_frames = linfo_ue(len,info); \
	INSERT_VALUE_TO_RANGE(num_ref_frames,num_ref_frames_range);\
	++bitoffset;\
	bitoffset += ( len = GetVLCSymbol(ctx->buf,bitoffset,&info,ctx->naluLen-(bitoffset/8)) ); \
	bitoffset += ( len = GetVLCSymbol(ctx->buf,bitoffset,&info,ctx->naluLen-(bitoffset/8)) ); \
	bitoffset += 3; \
	vui_prameters_present_flag = ( ctx->buf[bitoffset>>3] & (7-(bitoffset&0x07)) ) != 0; \
} \
if( (ctx->verbose==1 && (ctx->naluCount%256)==0 ) || ctx->verbose>=2 ) \
{ \
    fprintf(stderr,"NALUs: %ld, parsed %ld Mb, last NALU: size=%ld, type=%d" \
           ,ctx->naluCount,ctx->naluTotalBytes/(1024*1024),ctx->naluLen,ctx->naluType); \
    if(ctx->verbose==1) fprintf(stderr,"              \r"); else if(ctx->verbose>=2) fprintf(stderr,"\n"); \
} \
}while(0)

#define NALU_FINAL_SATS(ctx) do {\
int _c; \
  if(! ctx->briefStats) { \
	fprintf(stderr,"\n%ld NALUs, %ld bytes, sps [%d:%d], profile [%d:%d], ref_frames [%d:%d]\n" \
	,ctx->naluCount,ctx->naluTotalBytes \
	,ctx->sps_id_range[0],ctx->sps_id_range[1] \
	,ctx->profile_idc_range[0],ctx->profile_idc_range[1] \
	,ctx->num_ref_frames_range[0],ctx->num_ref_frames_range[1]); } \
  for(_c=0;_c<32;_c++) { \
	if( ! ctx->briefStats ) { \
		if(ctx->typeStats[_c]>0) { \
			fprintf(stderr,"NALU type %d count : %ld, %0.2F\%\n",_c,ctx->typeStats[_c],_c,ctx->typeStats[_c]*100.0/ctx->naluCount); \
		} \
	} \
	else { \
		fprintf(stderr, "%d:%d\n",_c,(int)( (ctx->typeStats[_c]*100)/ctx->naluCount ) ); \
	} \
  } \
} while(0)


#define EMIT_NALU(ctx) do {\
  if( ctx->writeOutput && ctx->naluLen>0) { \
	if( ctx->naluCount==1 && ctx->naluType!=AU_DELIMITER && ctx->insertAUD ) { \
		if(ctx->verbose) fprintf(stderr,"Inserting Access Unit Delimiter\n"); \
		if( ctx->naluStartCode ) { write(1,AccessUnitDelimiterSC,6); } \
		else { write(1,AccessUnitDelimiterLen,6); } } \
	if( ctx->naluStartCode ) { write(1,StartCode,4); } \
	else { write(1,ctx->sc,4); } \
	write(1,ctx->buf,ctx->naluLen); \
  } \
  NALU_STATS(ctx); \
} while(0)

#define CHECK_RESIZE_BUF(ctx) do {\
if( ctx->naluLen >= ctx->maxNaluSize ) { \
	ctx->maxNaluSize *= 2; \
	fprintf(stderr,"realloc NALU buffer to %ld bytes\n",ctx->maxNaluSize); \
	ctx->buf = (unsigned char*) realloc(ctx->buf,ctx->maxNaluSize); \
} \
} while(0)


int parseAnnexB(struct ParserCtx* ctx)
{
	while( ctx->naluCount<ctx->maxNALUs && readInput(ctx,ctx->buf+ctx->naluLen,1) == 1 )
	{
		++ctx->naluLen;
		if( ctx->buf[ctx->naluLen-1]==0x00 ) ++ ctx->zeros;
		else if( ctx->buf[ctx->naluLen-1]==0x01 && ctx->zeros==3 )
		{
			ctx->naluLen -= 4;
			ctx->sc[0] = (ctx->naluLen>>24) & 0xFF;
			ctx->sc[1] = (ctx->naluLen>>16) & 0xFF;
			ctx->sc[2] = (ctx->naluLen>>8) & 0xFF;
			ctx->sc[3] = ctx->naluLen & 0xFF;
			++ctx->naluCount;
			ctx->naluTotalBytes += ctx->naluLen;
			ctx->naluType = ctx->naluLen>0 ? (ctx->buf[0]&0x1F) : 0;
			EMIT_NALU(ctx);
			ctx->naluLen=0;
			ctx->zeros=0;
		}
		else { ctx->zeros=0; }
		CHECK_RESIZE_BUF(ctx);
	}
	EMIT_NALU(ctx);
	NALU_FINAL_SATS(ctx);
	return 1;
}

int parseMKVH264(struct ParserCtx* ctx)
{
	do 
	{
		ctx->naluLen = ( ((unsigned int)ctx->sc[0]) << 24 )
			| ( ((unsigned int)ctx->sc[1]) << 16 ) 
			| ( ((unsigned int)ctx->sc[2]) << 8 ) 
			| ((unsigned int)ctx->sc[3]) ;
		CHECK_RESIZE_BUF(ctx);

		size_t bytesRead = readInput(ctx,ctx->buf,ctx->naluLen);
		if( bytesRead < 0 )
		{
			fprintf(stderr,"I/O Error, aborting\n");
			return 0;
		}
		else if( bytesRead < ctx->naluLen )
		{
			fprintf(stderr,"Premature end of file, aborting\n");
			return 0;
		}
		++ ctx->naluCount;
		ctx->naluTotalBytes += ctx->naluLen;
		ctx->naluType = ctx->naluLen>0 ? (ctx->buf[0]&0x1F) : 0;
		EMIT_NALU(ctx);
	} while( ctx->naluCount<ctx->maxNALUs && readInput(ctx,ctx->sc,4) == 4 );

	NALU_FINAL_SATS(ctx);

	return 1;
}

int main(int argc, char* argv[])
{
	struct ParserCtx* ctx=NULL;
	int i,ok;
	ctx = allocContext();

	for(i=1;i<argc;i++)
	{
		if( strcmp(argv[i],"-mkv") == 0 ) ctx->naluStartCode = 0;
		else if( strcmp(argv[i],"-annexb") == 0 ) ctx->naluStartCode = 1;		
		else if( strcmp(argv[i],"-stat") == 0 ) ctx->writeOutput = 0;
		else if( strcmp(argv[i],"-b") == 0 ) ctx->briefStats = 1;
		else if( strcmp(argv[i],"-aud") == 0 ) ctx->insertAUD = 1;
		else if( strcmp(argv[i],"-nalucount") == 0 ) { ++i; ctx->maxNALUs = atoi(argv[i]); }
		else if( strcmp(argv[i],"-v") == 0 ) { ++ ctx->verbose; }
	}

	if( readInput(ctx,ctx->sc,4) != 4 ) return 1;
	if( ctx->sc[0]==0x00 && ctx->sc[1]==0x00 && ctx->sc[2]==0x00 && ctx->sc[3]==0x01 )
	{
		fprintf(stderr,"Reading Annex B input\n");
		ok = parseAnnexB(ctx);
	}
	else
	{
		fprintf(stderr,"Reading MKV's raw h264 format\n");
		ok = parseMKVH264(ctx);
	}

	freeContext(ctx);
	return (ok==0);
}

