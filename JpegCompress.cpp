#include <iostream>
#include "JpegCompress.h"

JpegCompresser::JpegCompresser()
{
  cinfo.err = jpeg_std_error( &jerr );
  jpeg_create_compress( &cinfo );
  originalImage = NULL;
  jpegImage = NULL;
  outlen = 0;
}

JpegCompresser::~JpegCompresser()
{
  if(jpegImage)
    free(jpegImage);
}

bool JpegCompresser::setImage(unsigned char* image, int width_, int height_, J_COLOR_SPACE space)
{
  originalImage = image;
  switch(space){
  case JCS_GRAYSCALE:
    size = 1;
    break;
  case JCS_RGB:
    size = 3;
    break;
  default:
    return false;
  }

  cinfo.image_width = width = width_;
  cinfo.image_height = height = height_;
  cinfo.input_components = size;
  cinfo.in_color_space = space;
  jpeg_set_defaults( &cinfo );
  return true;
}

void JpegCompresser::setCompression(int compression)
{
  jpeg_set_quality(&cinfo, 100-compression, TRUE );
}

void JpegCompresser::compress()
{
  jpeg_mem_dest(&cinfo, &jpegImage, &outlen); 
  jpeg_start_compress( &cinfo, TRUE );
  
  JSAMPROW row_pointer[1];
  for (int i = 0; i < height; i++ ) {
    row_pointer[0] = &originalImage[width*size*i]; 
    jpeg_write_scanlines( &cinfo, row_pointer, 1 );
  }
  
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
}


