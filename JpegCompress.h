#ifndef JPEGCOMPRESS_H
#define JPEGCOMPRESS_H
#include <stdlib.h>
#include <stdio.h>
#include <jpeglib.h>

class JpegCompresser
{
  public:
  JpegCompresser();
  ~JpegCompresser();
  bool setImage(unsigned char* image, int width, int height, J_COLOR_SPACE space);
  void setCompression(int compression);
  void compress();
  unsigned char* getJpegImage() {return jpegImage; };
  unsigned long getJpegImagelen() { return outlen; };

  private:
  int size;
  int width, height;
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  unsigned char* originalImage;
  unsigned char* jpegImage;
  unsigned long outlen;
};

#endif
