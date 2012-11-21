// -*- C++ -*-
/*!
 * @file  Axis282.cpp
 * @brief Axis282 Component
 * @date $Date$
 *
 * $Id$
 */

#include "Axis282.h"
#include <stdint.h>
#include <microhttpd.h>
#include <opencv/cv.h>
#include "internal.h"
#include "JpegCompress.h"

#define DEFAULT_FPS 30
#define DEFAULT_COMPRESSION 80
#define JPEGIMAGE_WIDTH 320
#define JPEGIMAGE_HEIGHT 240

struct Axis282_Param
{
  int fps;
  int compression;
};

static int get_argument(void *cls,
	                enum MHD_ValueKind kind,
	                const char *key,
	                const char *value)
{
  Axis282_Param* ap = (Axis282_Param *)cls;
  if (0 == strcmp(key, "fps"))
    ap->fps = atoi(value);
  else if (0 == strcmp(key, "compression"))
    ap->compression = atoi(value);
  return MHD_YES;
}

int answer_to_connection (void *cls, struct MHD_Connection *connection, 
                          const char *url, 
                          const char *method, const char *version, 
                          const char *upload_data, 
                          size_t *upload_data_size, void **con_cls)
{
  static const char* header0 = "HTTP/1.0 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=myboundary\r\n\r\n--myboundary\r\n";
  static const char* header = "Content-Type: image/jpeg\r\nContent-Length: ";
  static const char* footer = "--myboundary\r\n";
  char* message = NULL;

  std::cout << method << " " << url << " " << version << std::endl;

  if (0 != strcmp(method, "GET"))
    return MHD_NO; 
  
  Axis282* myComp = (Axis282 *)cls;
  if(strstr(url, "/mjpg/video.mjpg" ) == NULL ){
    if(strcmp(url, "/close" ) == 0 )
      myComp->sendImage = false;
    return MHD_NO;
  }

  myComp->sendImage = true;
  Axis282_Param ap;
  ap.fps = DEFAULT_FPS;
  ap.compression = DEFAULT_COMPRESSION;
  MHD_get_connection_values (connection,
			     MHD_GET_ARGUMENT_KIND,
			     &get_argument,
			     &ap);
  //std::cout << ap.fps << " " << ap.compression << std::endl;

  if( send(connection->socket_fd, (void *)header0, strlen (header0), 0) == -1 ){
    return MHD_NO;
  }
  	
  IplImage* inImageBuff = NULL;
  IplImage* outImageBuff = NULL;
  unsigned long count;
  double first_time;
  double old_time;
  bool first = true;
  J_COLOR_SPACE color_space = JCS_UNKNOWN;
  
  while(myComp->sendImage){
   pthread_mutex_lock(&myComp->mutex);
   double now = myComp->time;
   pthread_mutex_unlock(&myComp->mutex);
   if(now >=0){
     if(first){
	first_time = now;
        old_time = -1;
        count = 0;
        first = false;
      }
      if( now < first_time ){
        first_time = now;
        count = 0;
        old_time = -1;
      }
      if( now - old_time >0 && now - first_time >= 1.0/ap.fps*count ){
        //std::cout << connection->socket_fd << " time= " << myComp->time << std::endl;
        count++;
        old_time = now;

        pthread_mutex_lock(&myComp->mutex);
        Img::ColorFormat o_format = myComp->getImageptr()->data.image.format;
        int o_width = myComp->getImageptr()->data.image.width;
        int o_height = myComp->getImageptr()->data.image.height;
        // image size or format changed
        if(inImageBuff != NULL){
          int channels=0;
          switch(o_format){
          case Img::CF_RGB:
            channels = 3;
            break;
          case Img::CF_GRAY:
            channels = 1;
            break;
	  default:
            break;
          }
          if( o_width != inImageBuff->width || o_height != inImageBuff->height ||
              channels != inImageBuff->nChannels){
              cvReleaseImage(&inImageBuff);
              cvReleaseImage(&outImageBuff);
          }
        }
        //  create buffer 
        if(inImageBuff == NULL){
          int channels=0;
          switch(o_format){
          case Img::CF_RGB:
            channels = 3;
            color_space = JCS_RGB;
            break;
          case Img::CF_GRAY:
            color_space = JCS_GRAYSCALE;
            channels = 1;
            break;
	  default:
            color_space = JCS_UNKNOWN;
            break;
          }
          inImageBuff = cvCreateImage(cvSize(o_width, o_height), IPL_DEPTH_8U, channels);
          outImageBuff = cvCreateImage(cvSize(JPEGIMAGE_WIDTH, JPEGIMAGE_HEIGHT), IPL_DEPTH_8U, channels);
        }
        // resize
        int originalImage_len = inImageBuff->width * inImageBuff->height * inImageBuff->nChannels;
        memcpy(inImageBuff->imageData, myComp->getImageptr()->data.image.raw_data.get_buffer(), originalImage_len);
        cvResize (inImageBuff, outImageBuff, CV_INTER_LINEAR);
        pthread_mutex_unlock(&myComp->mutex);

        // ->jpeg
        JpegCompresser* compresser = new JpegCompresser;
        if(!compresser->setImage((unsigned char*)outImageBuff->imageData, outImageBuff->width, outImageBuff->height, color_space)){
          std::cout << "compresser error" << std::endl;
          delete compresser;
          break;       
	}
        compresser->setCompression(ap.compression);
        compresser->compress();
        unsigned char* jpegImage = compresser->getJpegImage();
        unsigned long length = compresser->getJpegImagelen();
 
        // send
        if( jpegImage != NULL && length != 0 ){
          int header_len = strlen(header)+4;
          char buf[10];
          sprintf(buf, "%ld", length);
          header_len += strlen(buf);
          int message_len = header_len + length + 2 + strlen(footer);
          message = (char*)malloc(sizeof(char)*message_len);
    	  sprintf( message, "%s%s\r\n\r\n", header, buf);
          memcpy( &message[header_len], jpegImage, length );
          memcpy( &message[header_len+length], "\r\n", 2 );
          memcpy( &message[header_len+length+2], footer, strlen(footer) );
	  if(send(connection->socket_fd, (void *)message, message_len, 0)  == -1){
            std::cout << "image send error" << std::endl;
	    free(message);
            delete compresser;
	    break;
          }
        }
	if(message!=NULL)
          free( message );
	delete compresser;
      }
    }
    usleep(10000);
  }  

  if(inImageBuff != NULL)
    cvReleaseImage(&inImageBuff);
  if(outImageBuff != NULL)
    cvReleaseImage(&outImageBuff);

  return MHD_NO;
}

// Module specification
// <rtc-template block="module_spec">
static const char* axis282_spec[] =
  {
    "implementation_id", "Axis282",
    "type_name",         "Axis282",
    "description",       "Axis282 Component",
    "version",           "1.1.0",
    "vendor",            "AIST",
    "category",          "Category",
    "activity_type",     "PERIODIC",
    "kind",              "DataFlowComponent",
    "max_instance",      "1",
    "language",          "C++",
    "lang_type",         "compile",
    "conf.default.httpPort", "8880",
    ""
  };
// </rtc-template>

/*!
 * @brief constructor
 * @param manager Maneger Object
 */
Axis282::Axis282(RTC::Manager* manager)
    // <rtc-template block="initializer">
  : RTC::DataFlowComponentBase(manager),
    m_originalImageIn("originalImage", m_originalImage)

    // </rtc-template>
{
}

/*!
 * @brief destructor
 */
Axis282::~Axis282()
{
}



RTC::ReturnCode_t Axis282::onInitialize()
{
  //std::cout << m_profile.instance_name << ": onInitialize()" << std::endl;
  bindParameter("httpPort", m_port, "8880" );

  // Registration: InPort/OutPort/Service
  // <rtc-template block="registration">
  // Set InPort buffers
  addInPort("originalImage", m_originalImageIn);
  
  // Set OutPort buffer
  
  // Set service provider to Ports
  
  // Set service consumers to Ports
  
  // Set CORBA Service Ports
  
  // </rtc-template>

  return RTC::RTC_OK;
}

/*
RTC::ReturnCode_t Axis282::onFinalize()
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis282::onStartup(RTC::UniqueId _ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis282::onShutdown(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/


RTC::ReturnCode_t Axis282::onActivated(RTC::UniqueId ec_id)
{
  std::cout << m_profile.instance_name << ": httpPort= " << m_port << std::endl;
  time = -1;
  sendImage = false;

  pthread_mutex_init(&mutex,NULL);

  daemon = MHD_start_daemon (MHD_USE_THREAD_PER_CONNECTION, m_port, NULL, NULL, 
                             &answer_to_connection, this, MHD_OPTION_END);
  if (NULL == daemon) 
    return RTC::RTC_ERROR;
  return RTC::RTC_OK;
}


RTC::ReturnCode_t Axis282::onDeactivated(RTC::UniqueId ec_id)
{
  sendImage = false;
  MHD_stop_daemon(daemon);
  pthread_mutex_destroy(&mutex);
  return RTC::RTC_OK;
}


RTC::ReturnCode_t Axis282::onExecute(RTC::UniqueId ec_id)
{
  //std::cout << m_profile.instance_name << ": onExecute()" << std::endl;
  
  pthread_mutex_lock(&mutex);
  if (m_originalImageIn.isNew()){
    do {
      m_originalImageIn.read();
    }while(m_originalImageIn.isNew());
    time = m_originalImage.tm.sec + m_originalImage.tm.nsec*1e-9;
  }  
  pthread_mutex_unlock(&mutex);
  return RTC::RTC_OK;
}

/*
RTC::ReturnCode_t Axis282::onAborting(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis282::onError(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis282::onReset(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis282::onStateUpdate(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis282::onRateChanged(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/



extern "C"
{
 
  void Axis282Init(RTC::Manager* manager)
  {
    coil::Properties profile(axis282_spec);
    manager->registerFactory(profile,
                             RTC::Create<Axis282>,
                             RTC::Delete<Axis282>);
  }
  
};


