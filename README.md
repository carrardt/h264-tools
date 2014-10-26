h264-tools
===========

scripts:
	mkv2mkv input-file output-file

ldecod:
	Doc: H.264+MVC Decoder extracted and modified from JM 18.6 reference implementation
	Usage: ldecod ...

tools:
	naluparser
	    Doc: parses H.264 Annex B or Cluster based (from mkv) streams
	    Usage: naluparser [-stat] [-v] [-nalucount #] [-mkv] [-annexb] < inputFile > outputFile
	yuvsbspipe
	    Doc: reads to YUV streams from 2 files/named pipes and output a single YUV Side-By-Side stream to a file/named pipe
	    Usage: yuvsbspipe -w <width> -h <height> -l left-stream.yuv -r right-stream.yuv -o output-sbs.yuv

