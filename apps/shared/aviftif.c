// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "aviftif.h"
/* We cannot change the reality of libtif */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreserved-id-macro"
#include <tiffio.h>
#pragma GCC diagnostic pop
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Note on setjmp() and volatile variables:
//
// K & R, The C Programming Language 2nd Ed, p. 254 says:
//   ... Accessible objects have the values they had when longjmp was called,
//   except that non-volatile automatic variables in the function calling setjmp
//   become undefined if they were changed after the setjmp call.
//
// Therefore, 'rowPointers' is declared as volatile. 'rgb' should be declared as
// volatile, but doing so would be inconvenient (try it) and since it is a
// struct, the compiler is unlikely to put it in a register. 'readResult' and
// 'writeResult' do not need to be declared as volatile because they are not
// modified between setjmp and longjmp. But GCC's -Wclobbered warning may have
// trouble figuring that out, so we preemptively declare them as volatile.

avifBool avifTIFRead(avifImage * avif, const char * inputFilename, avifPixelFormat requestedFormat, uint32_t requestedDepth, uint32_t * outTIFDepth)
{
    volatile avifBool readResult = AVIF_FALSE;
    TIFF *tif = NULL;
    avifRGBImage rgb = {0};
    uint32 width, height, npixels;
    uint32 *raster = NULL;
    if (!inputFilename) {
        fprintf(stderr, "TIFF files require random access, can't read from stdin\n");
        goto cleanup;
    }	
    
    if (!(tif = TIFFOpen(inputFilename, "rM"))){
        fprintf(stderr, "Can't open TIFF file for read: %s\n", inputFilename);
        goto cleanup;
    }
    uint32 bpp, config, nsamples;
    if (TIFFGetField(tif,TIFFTAG_IMAGEWIDTH,&width) && 
	TIFFGetField(tif,TIFFTAG_IMAGELENGTH,&height) &&
	TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bpp) &&
	TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &nsamples) &&
	TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &config)
	){
	npixels = width * height;
	raster = (uint32 *) _TIFFmalloc(npixels*sizeof(uint32));

	if (raster) {
	    unsigned char * iccpData = NULL;
	    uint32 iccpDataLen = 0;
	    if (TIFFGetField(tif, TIFFTAG_ICCPROFILE, &iccpDataLen, &iccpData))
	    {
		avifImageSetProfileICC(avif, iccpData, iccpDataLen);
	    }
	    int imgBitDepth = 8;
	    if (outTIFDepth) {
		*outTIFDepth = imgBitDepth;
	    }
	    avif->width = width;
	    avif->height = height;
	    avif->yuvFormat = requestedFormat;
	    avif->depth = requestedDepth;
	    if (avif->depth == 0) {
		if (imgBitDepth == 8) {
		    avif->depth = 8;
		} else {
		    avif->depth = 12;
		}
	    }
	    avifRGBImageSetDefaults(&rgb, avif);
	    rgb.depth = imgBitDepth;
	    avifRGBImageAllocatePixels(&rgb);
	    uint32 bytesPerRow = width*sizeof(uint32);
	    /* seems to render rows in reverse order, reading with
	       orientation might fix this but didn't work when i tried */
	    if (TIFFReadRGBAImage(tif,width,height,raster,0)){
		char *rex = (char *)raster;
		uint32 row=height;
		do {
		    row--;
		    memcpy(rgb.pixels+row*bytesPerRow,rex,bytesPerRow);
		    rex += bytesPerRow;
		} while(row!=0);
	    }
	    if (avifImageRGBToYUV(avif, &rgb) != AVIF_RESULT_OK) {
		fprintf(stderr, "Conversion to YUV failed: %s\n", inputFilename);
		goto cleanup;
	    }
	    readResult = AVIF_TRUE;
	}
    }
cleanup:
    if (raster) {
	_TIFFfree(raster);
    }
    if (tif) {
	TIFFClose(tif);
    }
    avifRGBImageFreePixels(&rgb);
    return readResult;
}

