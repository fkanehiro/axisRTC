// -*- C++ -*-
/*!
 * @file  Axis241Q.cpp
 * @brief Axis241Q Component
 * @date $Date$
 *
 * $Id$
 */

#include "Axis241Q.h"
#include <stdint.h>
#include <microhttpd.h>
#include <opencv/cv.h>
#include "internal.h"
#include "JpegCompress.h"

#define DEFAULT_FPS 30
#define DEFAULT_COMPRESSION 80
#define DEFAULT_MIRROR false
#define JPEGIMAGE_WIDTH 704
#define JPEGIMAGE_HEIGHT 480

struct Axis241Q_Param
{
  int fps;
  int compression;
  bool mirror;
};

static int get_argument(void *cls,
	                enum MHD_ValueKind kind,
	                const char *key,
	                const char *value)
{
  Axis241Q_Param* ap = (Axis241Q_Param *)cls;
  if (0 == strcmp(key, "fps"))
    ap->fps = atoi(value);
  else if (0 == strcmp(key, "compression"))
    ap->compression = atoi(value);
  else if (0 == strcmp(key, "mirror"))
    ap->mirror = value[0]=='1' ? true : false;
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
  
  Axis241Q* myComp = (Axis241Q *)cls;
  if(strstr(url, "/mjpg/quad/video.mjpg" ) == NULL ){
    if(strcmp(url, "/close" ) == 0 )
      myComp->sendImage = false;
    return MHD_NO;
  }
  
  myComp->sendImage = true;
  Axis241Q_Param ap;
  ap.fps = DEFAULT_FPS;
  ap.compression = DEFAULT_COMPRESSION;
  ap.mirror = DEFAULT_MIRROR;
  MHD_get_connection_values (connection,
			     MHD_GET_ARGUMENT_KIND,
			     &get_argument,
			     &ap); 
  std::cout << ap.fps << " " << ap.compression << " " << ap.mirror << std::endl;

  if( send(connection->socket_fd, (void *)header0, strlen (header0), 0) == -1 ){
    return MHD_NO;
  }

  IplImage* inImageBuff = NULL;
  IplImage* mirrorImageBuff = NULL;
  IplImage* outImageBuff = NULL;
  unsigned long count;
  double first_time;
  double old_time;
  bool first = true;
  J_COLOR_SPACE color_space = JCS_UNKNOWN;
  
  while(myComp->sendImage){
   pthread_mutex_lock(&myComp->mutex);
   double now = myComp->maxtime;
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
        
        TimedCameraImage* timedCameraImage[4];
        int out_channels=0;
        int channels[4]={0};
        pthread_mutex_lock(&myComp->mutex);
        for(int i=0; i<4; i++){
          timedCameraImage[i] = myComp->getImageptr(i);
          if(myComp->time[i]>=0){
            Img::ColorFormat o_format = timedCameraImage[i]->data.image.format;
            switch(o_format){
            case Img::CF_RGB:
              channels[i] = 3;
              if(out_channels<3) out_channels = 3;
              break;
            case Img::CF_GRAY:
              channels[i] = 1;
              if(out_channels<1) out_channels = 1;
              break;
	    default:
              break;
            }
          }
        }
        if(outImageBuff != NULL && out_channels != outImageBuff->nChannels)
          cvReleaseImage(&outImageBuff);
        if(outImageBuff == NULL )
          outImageBuff = cvCreateImage(cvSize(JPEGIMAGE_WIDTH, JPEGIMAGE_HEIGHT), IPL_DEPTH_8U, out_channels);

        for(int i=0; i<4; i++){
          CvRect roi = cvRect (0, 0, JPEGIMAGE_WIDTH/2, JPEGIMAGE_HEIGHT/2);
          roi.x = JPEGIMAGE_WIDTH/2 * (i%2);
          roi.y = JPEGIMAGE_HEIGHT/2 * (int)(i/2);
          cvSetImageROI (outImageBuff, roi);
          if(myComp->time[i]<0){
            cvZero (outImageBuff);
          }else{
            int o_width = timedCameraImage[i]->data.image.width;
            int o_height = timedCameraImage[i]->data.image.height;
            if(inImageBuff != NULL){  
              if( o_width != inImageBuff->width || o_height != inImageBuff->height ||
                out_channels != inImageBuff->nChannels){
                  cvReleaseImage(&inImageBuff);
                  if(mirrorImageBuff != NULL)
                    cvReleaseImage(&mirrorImageBuff);
              }
            }
            if(inImageBuff == NULL){
              inImageBuff = cvCreateImage(cvSize(o_width, o_height), IPL_DEPTH_8U, out_channels);
            }
            int originalImage_len = inImageBuff->width * inImageBuff->height * inImageBuff->nChannels;
            if(channels[i]==out_channels)
  	      memcpy(inImageBuff->imageData, timedCameraImage[i]->data.image.raw_data.get_buffer(), originalImage_len);
            else{  // out_channels=3, channels=1
              unsigned char* src = timedCameraImage[i]->data.image.raw_data.get_buffer();
              for(int j=0, k=0; j<originalImage_len; ){
	        inImageBuff->imageData[j++] = src[k];
                inImageBuff->imageData[j++] = src[k];
                inImageBuff->imageData[j++] = src[k++];
              }
            }

            if(ap.mirror){
              if(mirrorImageBuff == NULL){
                mirrorImageBuff = cvCreateImage(cvSize(o_width, o_height), IPL_DEPTH_8U, out_channels);
              }
              cvFlip( inImageBuff, mirrorImageBuff, 1);
              cvResize( mirrorImageBuff, outImageBuff, CV_INTER_LINEAR);
            }else
              cvResize( inImageBuff, outImageBuff, CV_INTER_LINEAR);
          }
        }
        cvResetImageROI (outImageBuff);
        pthread_mutex_unlock(&myComp->mutex);

        if(out_channels==1)
          color_space = JCS_GRAYSCALE;
        else if(out_channels==3)
          color_space = JCS_RGB;
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
    usleep(1000);
  }  

  if(inImageBuff != NULL)
    cvReleaseImage(&inImageBuff);
  if(outImageBuff != NULL)
    cvReleaseImage(&outImageBuff);
  if(mirrorImageBuff != NULL)
    cvReleaseImage(&mirrorImageBuff);

  return MHD_NO;
}

// Module specification
// <rtc-template block="module_spec">
static const char* axis241Q_spec[] =
  {
    "implementation_id", "Axis241Q",
    "type_name",         "Axis241Q",
    "description",       "Axis241Q Component",
    "version",           "1.1.0",
    "vendor",            "AIST",
    "category",          "Category",
    "activity_type",     "PERIODIC",
    "kind",              "DataFlowComponent",
    "max_instance",      "1",
    "language",          "C++",
    "lang_type",         "compile",
    "conf.default.httpPort", "8881",
    ""
  };
// </rtc-template>

/*!
 * @brief constructor
 * @param manager Maneger Object
 */
Axis241Q::Axis241Q(RTC::Manager* manager)
    // <rtc-template block="initializer">
  : RTC::DataFlowComponentBase(manager),
    m_originalImage0In("originalImage0", m_originalImage0),
    m_originalImage1In("originalImage1", m_originalImage1),
    m_originalImage2In("originalImage2", m_originalImage2),
    m_originalImage3In("originalImage3", m_originalImage3)
    // </rtc-template>
{
}

/*!
 * @brief destructor
 */
Axis241Q::~Axis241Q()
{
}



RTC::ReturnCode_t Axis241Q::onInitialize()
{
  //std::cout << m_profile.instance_name << ": onInitialize()" << std::endl;
  bindParameter("httpPort", m_port, "8881" );

  // Registration: InPort/OutPort/Service
  // <rtc-template block="registration">
  // Set InPort buffers
  addInPort("originalImage0", m_originalImage0In);
  addInPort("originalImage1", m_originalImage1In);
  addInPort("originalImage2", m_originalImage2In);
  addInPort("originalImage3", m_originalImage3In);
  
  // Set OutPort buffer
  
  // Set service provider to Ports
  
  // Set service consumers to Ports
  
  // Set CORBA Service Ports
  
  // </rtc-template>
  
  return RTC::RTC_OK;
}

/*
RTC::ReturnCode_t Axis241Q::onFinalize()
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis241Q::onStartup(RTC::UniqueId _ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis241Q::onShutdown(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/


RTC::ReturnCode_t Axis241Q::onActivated(RTC::UniqueId ec_id)
{
  std::cout << m_profile.instance_name << ": httpPort= " << m_port << std::endl;
  for(int i=0; i<4; i++)
    time[i] = -1;
  sendImage = false;

  pthread_mutex_init(&mutex,NULL);

  daemon = MHD_start_daemon (MHD_USE_THREAD_PER_CONNECTION, m_port, NULL, NULL, 
                             &answer_to_connection, this, MHD_OPTION_END);
  if (NULL == daemon) 
    return RTC::RTC_ERROR;

  return RTC::RTC_OK;
}


RTC::ReturnCode_t Axis241Q::onDeactivated(RTC::UniqueId ec_id)
{
  sendImage = false;
  MHD_stop_daemon(daemon);
  pthread_mutex_destroy(&mutex);
  return RTC::RTC_OK;
}


RTC::ReturnCode_t Axis241Q::onExecute(RTC::UniqueId ec_id)
{
  //std::cout << m_profile.instance_name << ": onExecute()" << std::endl;
  pthread_mutex_lock(&mutex);
  if (m_originalImage0In.isNew()){
    do {
      m_originalImage0In.read();
    }while(m_originalImage0In.isNew());
    time[0] = m_originalImage0.tm.sec + m_originalImage0.tm.nsec*1e-9;
  }  
  if (m_originalImage1In.isNew()){
    do {
      m_originalImage1In.read();
    }while(m_originalImage1In.isNew());
    time[1] = m_originalImage1.tm.sec + m_originalImage1.tm.nsec*1e-9;
  }
  if (m_originalImage2In.isNew()){
    do {
      m_originalImage2In.read();
    }while(m_originalImage2In.isNew());
    time[2] = m_originalImage2.tm.sec + m_originalImage2.tm.nsec*1e-9;
  }
  if (m_originalImage3In.isNew()){
    do {
      m_originalImage3In.read();
    }while(m_originalImage3In.isNew());
    time[3] = m_originalImage3.tm.sec + m_originalImage3.tm.nsec*1e-9;
  }
  maxtime = time[0];
  for(int i=1; i<4; i++)
    maxtime = maxtime < time[i] ? time[i] : maxtime;
  pthread_mutex_unlock(&mutex);
  return RTC::RTC_OK;
}

/*
RTC::ReturnCode_t Axis241Q::onAborting(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis241Q::onError(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis241Q::onReset(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis241Q::onStateUpdate(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis241Q::onRateChanged(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/



extern "C"
{
 
  void Axis241QInit(RTC::Manager* manager)
  {
    coil::Properties profile(axis241Q_spec);
    manager->registerFactory(profile,
                             RTC::Create<Axis241Q>,
                             RTC::Delete<Axis241Q>);
  }
  
};


