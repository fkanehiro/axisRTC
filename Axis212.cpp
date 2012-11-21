// -*- C++ -*-
/*!
 * @file  Axis212.cpp
 * @brief Axis212 Component
 * @date $Date$
 *
 * $Id$
 */

#include "Axis212.h"
#include <stdint.h>
#include <microhttpd.h>
#include <opencv/cv.h>
#include "internal.h"
#include "JpegCompress.h"

#define DEFAULT_FPS 30
#define DEFAULT_COMPRESSION 80
#define JPEGIMAGE_WIDTH 640
#define JPEGIMAGE_HEIGHT 480
#define MAXHORIZONTALANGLE 140
#define MINIHORIZONTALANGLE 44
#define MAXVERTICALANGLE 105
#define MINIVERTICALANGLE 35
#define MAXZOOM 9999
#define INIMAGEWIDTH 2048
#define INIMAGEHEIGHT 1536

std::string pan_key[] = {"-48.2","-16.1","16","48.1"};
std::string tilt_key[] = {"36","12.1","-11.8","-35.7"};
int x_value[] = {0, 470, 940, 1408};
int y_value[] = {0, 355,710, 1065};
typedef std::map<std::string, int> PTtoXY_MAP;
PTtoXY_MAP pantox_map;
PTtoXY_MAP tilttoy_map;

enum AXIS212_COMMAND
{
  UNKNOWN = 0,
  SET,
  GET_POSITION,
  GET_IMAGE,
  GET_MJPEG,
  MJPEG_CONNECTION_CLOSE,
  ARG_ERROR = 99
};

enum AXIS212_SUB_COMMAND
{
  SUB_UNKNOWN = 0,
  LEFT,
  RIGHT,
  UP,
  DOWN,
  RZOOM,
  CENTER,
  PTZ,
  PTZ99999
};

struct Axis212_Argument
{
  AXIS212_SUB_COMMAND move;
  int zoom;
  int center_x, center_y;
  double pan, tilt;
  std::string pan_string, tilt_string;
  bool getPosition;
  int fps;
  int compression;
};

static int get_argument(void *cls,
	                enum MHD_ValueKind kind,
	                const char *key,
	                const char *value)
{
  Axis212_Argument* aa = (Axis212_Argument *)cls;
  if (0 == strcmp(key, "move")){
    if(strcmp(value, "left") == 0)
      aa->move = LEFT;
    else if(strcmp(value, "right") == 0)
      aa->move = RIGHT;
    else if(strcmp(value, "up") == 0)
      aa->move = UP;
    else if(strcmp(value, "down") == 0)
      aa->move = DOWN;
  }else if (0 == strcmp(key, "zoom")){
    aa->zoom = atoi(value);
  }else if (0 == strcmp(key, "center")){
    aa->center_x = atoi(strtok((char *)value, ","));
    aa->center_y = atoi(strtok(NULL, ","));
  }else if (0 == strcmp(key, "pan")){
    aa->pan = atof(value);
    aa->pan_string = std::string(value);
  }else if (0 == strcmp(key, "tilt")){
    aa->tilt = atof(value);
    aa->tilt_string = std::string(value);
  }else if (0 == strcmp(key, "query")){
    if(strcmp(value, "position") == 0)
      aa->getPosition = true;
  }else if (0 == strcmp(key, "fps")){
    aa->fps = atoi(value);
  }else if (0 == strcmp(key, "compression")){
    aa->compression = atoi(value);
  }
  return MHD_YES;
}

void parse_url(struct MHD_Connection *connection, const char* url, enum AXIS212_COMMAND& com,
               enum AXIS212_SUB_COMMAND& sub_com, Axis212_Argument* aa)
{
  aa->move = SUB_UNKNOWN;
  aa->zoom = -99999;
  aa->center_x = -1;
  aa->center_y = -1;
  aa->pan = 9999;
  aa->pan_string = "";
  aa->tilt = 9999;
  aa->tilt_string = "";
  aa->getPosition = false;
  aa->fps = -1;
  aa->compression = -1;
  MHD_get_connection_values (connection, MHD_GET_ARGUMENT_KIND, &get_argument, aa);
  if(strstr(url, "/axis-cgi/com/ptz.cgi" ) != NULL ){
    com = SET;
    if(aa->move){
      if( aa->zoom != -99999 || aa->center_x != -1 || aa->center_y != -1 || 
          aa->pan < 9999 || aa->tilt < 9999 || aa->getPosition != false ||
          aa->fps != -1 || aa->compression != -1){
        com = ARG_ERROR;
        return;
      }else{
        sub_com = aa->move;
        return;      
      }
    }else if(aa->zoom != -99999){
      if( aa->center_x != -1 || aa->center_y != -1 || aa->getPosition != false ||
          aa->fps != -1 || aa->compression != -1){
        com = ARG_ERROR;
        return;
      }else{
        if( aa->pan > 9998 && aa->tilt > 9998){
          if( aa->zoom != 3333 && aa->zoom != -3333 ){
            com = ARG_ERROR;
            return;
          }else{   //rzoom
            sub_com = RZOOM;
            return;
          }
        }else if( aa->pan > 9998 || aa->tilt > 9998){
            com = ARG_ERROR;
            return;
        }else{
          if( aa->zoom != 99999 ){   // pan, tilt, zoom
            sub_com = PTZ;
	    return;
          }else{  // zoom=99999
            sub_com = PTZ99999;
	    return;
          }
        }
      }
    }else if(aa->center_x != -1 || aa->center_y != -1){  // center
      if( aa->pan < 9999 || aa->tilt < 9999 || aa->getPosition != false ||
          aa->fps != -1 || aa->compression != -1 ||
          aa->center_x == -1 || aa->center_y == -1){
        com = ARG_ERROR;
        return;
      }else{
        sub_com = CENTER;
        return;
      }
    }else if(aa->getPosition){  //position
      if(aa->fps != -1 || aa->compression != -1 ){
        com = ARG_ERROR;
        return;
      }else{
        com = GET_POSITION;
        return;
      }
    }else{
      com = ARG_ERROR;
      return;
    }
  }else if( strstr(url, "/axis-cgi/jpg/image.cgi" ) != NULL ){
    if( aa->move || aa->zoom != -99999 || aa->center_x != -1 || aa->center_y != -1 || 
        aa->pan < 9999 || aa->tilt < 9999 || aa->getPosition != false ){
      com = ARG_ERROR;
      return;
    }else{
      com = GET_IMAGE;
      if(aa->fps == -1)
        aa->fps = DEFAULT_FPS;
      if(aa->compression == -1)
        aa->compression = DEFAULT_COMPRESSION;
      return;
    }
  }else if(strstr(url, "/mjpg/video.mjpg") != NULL ){
    if( aa->move || aa->zoom != -99999 || aa->center_x != -1 || aa->center_y != -1 || 
        aa->pan < 9999 || aa->tilt < 9999 || aa->getPosition != false ){
      com = ARG_ERROR;
      return;
    }else{
      com = GET_MJPEG;
      if(aa->fps == -1)
        aa->fps = DEFAULT_FPS;
      if(aa->compression == -1)
        aa->compression = DEFAULT_COMPRESSION;
      return;
    }
  }else if(strcmp(url, "/close" ) == 0 ){
      com = MJPEG_CONNECTION_CLOSE;
      return;
  }else
    com = UNKNOWN;
}

void sendResponse(struct MHD_Connection *connection, const char * message)
{
  struct MHD_Response * response = MHD_create_response_from_data(strlen(message), (void*) message,
                                                                 MHD_NO, MHD_YES);
  MHD_add_response_header (response, "Content-Type", "text/plain");
  MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);
}

int imgformattochannel(Img::ColorFormat format)
{
  switch(format){
      case Img::CF_RGB:
        return 3;
      case Img::CF_GRAY:
        return 1;
      default:
        ;
  }
  return 0;
}

J_COLOR_SPACE imgformattoJCS(Img::ColorFormat format)
{
  switch(format){
    case Img::CF_RGB:
      return JCS_RGB;
    case Img::CF_GRAY:
      return JCS_GRAYSCALE;
    default:
      ;
  }
  return JCS_UNKNOWN;
}

void clipImage(IplImage*& inImageBuff, IplImage*& outImageBuff, Axis212* myComp, Img::ColorFormat format )
{
  //  create buffer 
  if(inImageBuff == NULL){
    int channels=imgformattochannel(format);
    inImageBuff = cvCreateImage(cvSize(myComp->width, myComp->height), IPL_DEPTH_8U, channels);
    outImageBuff = cvCreateImage(cvSize(JPEGIMAGE_WIDTH, JPEGIMAGE_HEIGHT), IPL_DEPTH_8U, channels);
  }
  // clip
  int originalImage_len = inImageBuff->width * inImageBuff->height * inImageBuff->nChannels;
  memcpy(inImageBuff->imageData, myComp->getImageptr()->data.image.raw_data.get_buffer(), originalImage_len);
  CvRect roi = cvRect (myComp->clip_x, myComp->clip_y, myComp->clip_width, myComp->clip_height );
  cvSetImageROI (inImageBuff, roi);
  cvResize (inImageBuff, outImageBuff, CV_INTER_LINEAR);
}

int answer_to_connection (void *cls, struct MHD_Connection *connection, 
                          const char *url, 
                          const char *method, const char *version, 
                          const char *upload_data, 
                          size_t *upload_data_size, void **con_cls)
{
  if (0 != strcmp(method, "GET"))
    return MHD_NO;
  static int dummy;
  if (&dummy != *con_cls){
    *con_cls = &dummy;
    return MHD_YES;
  }
  *con_cls = NULL;

  std::cout << method << " " << url << " " << version << std::endl;

  Axis212* myComp = (Axis212 *)cls;
  AXIS212_COMMAND command;
  AXIS212_SUB_COMMAND sub_command;
  Axis212_Argument aa;
  parse_url(connection, url, command, sub_command, &aa);

  switch(command){
  case ARG_ERROR :
    sendResponse(connection, "invalid parameter\r\n");
    return MHD_YES;
  case SET : {
    bool calcClipParam = false;
    double hangle, vangle;
    switch(sub_command){
    case LEFT :
      pthread_mutex_lock(&myComp->mutex);
      hangle = myComp->getHorizontalAngle();
      myComp->setPan(myComp->pan - hangle/4);
      pthread_mutex_unlock(&myComp->mutex);
      calcClipParam = true;
      break;
    case RIGHT :
      pthread_mutex_lock(&myComp->mutex);
      hangle = myComp->getHorizontalAngle();
      myComp->setPan(myComp->pan + hangle/4);
      pthread_mutex_unlock(&myComp->mutex);
      calcClipParam = true;
      break;
    case UP : 
      pthread_mutex_lock(&myComp->mutex);
      vangle = myComp->getVerticalAngle();
      myComp->setTilt(myComp->tilt - vangle/4);
      pthread_mutex_unlock(&myComp->mutex);
      calcClipParam = true;
      break;
    case DOWN :
      pthread_mutex_lock(&myComp->mutex);
      vangle = myComp->getVerticalAngle();
      myComp->setTilt(myComp->tilt + vangle/4);
      pthread_mutex_unlock(&myComp->mutex);
      calcClipParam = true;
      break;
    case RZOOM :
      pthread_mutex_lock(&myComp->mutex);
      myComp->setZoom(myComp->zoom + aa.zoom);
      pthread_mutex_unlock(&myComp->mutex);
      calcClipParam = true;
      break;
    case CENTER :
      pthread_mutex_lock(&myComp->mutex);
      myComp->setCenterX(aa.center_x);
      myComp->setCenterY(aa.center_y);
      pthread_mutex_unlock(&myComp->mutex);
      calcClipParam = false;
      break;
    case PTZ :
      pthread_mutex_lock(&myComp->mutex);
      myComp->setZoom( aa.zoom );
      myComp->setPan(aa.pan);
      myComp->setTilt(aa.tilt);	
      pthread_mutex_unlock(&myComp->mutex); 
      calcClipParam = true;
      break;
    case PTZ99999 :
      pthread_mutex_lock(&myComp->mutex);
      myComp->clip_width = JPEGIMAGE_WIDTH;
      myComp->clip_height = JPEGIMAGE_HEIGHT;
      {
        PTtoXY_MAP::iterator it = pantox_map.find(aa.pan_string);
        if(it != pantox_map.end())
          myComp->clip_x = it->second;
        else
          myComp->clip_x = 0;
        it = tilttoy_map.find(aa.tilt_string);
        if(it != tilttoy_map.end())
          myComp->clip_y = it->second;
        else
          myComp->clip_y = 0;
      }
      pthread_mutex_unlock(&myComp->mutex);
      calcClipParam = false;
      break;
    case SUB_UNKNOWN :
    default :
      sendResponse(connection, "invalid parameter\r\n");
      return MHD_NO;
    }
    if(calcClipParam){
      pthread_mutex_lock(&myComp->mutex);
      myComp->PTZtoClipParam();
      pthread_mutex_unlock(&myComp->mutex); 
    }
    std::cout << myComp->clip_width << " " << myComp->clip_height << " " << myComp->clip_x << " " << myComp->clip_y << std::endl;
    sendResponse(connection, "AXIS 212 PTZ Netwrok Camera\r\n");
    return MHD_YES; }
  case GET_POSITION : {
    pthread_mutex_lock(&myComp->mutex);
    double pan = myComp->pan;
    double tilt = myComp->tilt;
    int zoom = myComp->zoom;
    pthread_mutex_unlock(&myComp->mutex);
    char mess[100];
    sprintf(mess, "pan=%.2f\r\ntilt=%.2f\r\nzoom=%d\r\n", pan, tilt, zoom);
    sendResponse(connection, mess);
    return MHD_YES; }
  case GET_IMAGE : {
    IplImage* inImageBuff = NULL;
    IplImage* outImageBuff = NULL;
    pthread_mutex_lock(&myComp->mutex);
    Img::ColorFormat o_format = myComp->getImageptr()->data.image.format;
    clipImage(inImageBuff, outImageBuff, myComp, o_format );
    pthread_mutex_unlock(&myComp->mutex);

    J_COLOR_SPACE color_space = imgformattoJCS(o_format);
    JpegCompresser compresser;
    if(!compresser.setImage((unsigned char*)outImageBuff->imageData, outImageBuff->width, outImageBuff->height, color_space)){
      std::cout << "compresser error" << std::endl;
    }else{
      std::cout << "compression= " << aa.compression << std::endl;
      compresser.setCompression(aa.compression);
      compresser.compress();
    }
    unsigned char* jpegImage = compresser.getJpegImage();
    unsigned long length = compresser.getJpegImagelen();
    if(inImageBuff != NULL)
      cvReleaseImage(&inImageBuff);
    if(outImageBuff != NULL)
      cvReleaseImage(&outImageBuff);

    if( jpegImage != NULL && length != 0){
      char * data = (char*)malloc(sizeof(char)*length +2);
      memcpy( data, jpegImage, length );
      memcpy( &data[length], "\r\n", 2 );
      struct MHD_Response * response = MHD_create_response_from_data(length+2, (void*) data, MHD_YES, MHD_NO);
      MHD_add_response_header (response, "Content-Type", "image/jpeg");
      char buf[10];
      sprintf(buf, "%ld", length);
      MHD_add_response_header (response, "Content-Length", buf);
      MHD_queue_response(connection, MHD_HTTP_OK, response);
      MHD_destroy_response(response);
    }else{
      sendResponse(connection, "Jpeg compresser error\r\n");
    }
    return MHD_YES; }

  case GET_MJPEG : {
    static const char* header0 = "HTTP/1.0 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=myboundary\r\n\r\n--myboundary\r\n";
    static const char* header = "Content-Type: image/jpeg\r\nContent-Length: ";
    static const char* footer = "--myboundary\r\n";
  
    if( send(connection->socket_fd, (void *)header0, strlen (header0), 0) == -1 ){
      return MHD_NO;
    }
    
    IplImage* inImageBuff = NULL;
    IplImage* outImageBuff = NULL;
    unsigned long count;
    double first_time;
    double old_time;
    bool first = true;
    myComp->sendImage = true;

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
      if( now - old_time >0 && now - first_time >= 1.0/aa.fps*count ){
        //std::cout << connection->socket_fd << " time= " << myComp->time << std::endl;
        count++;
        old_time = now;

        pthread_mutex_lock(&myComp->mutex);
        Img::ColorFormat o_format = myComp->getImageptr()->data.image.format;
        // image size or format changed
        if(inImageBuff != NULL){
          int channels=imgformattochannel(o_format);
          if( myComp->width != inImageBuff->width || myComp->height != inImageBuff->height ||
              channels != inImageBuff->nChannels){
              cvReleaseImage(&inImageBuff);
              cvReleaseImage(&outImageBuff);
          }
        }
        clipImage(inImageBuff, outImageBuff, myComp, o_format );
        pthread_mutex_unlock(&myComp->mutex);

        // ->jpeg
        J_COLOR_SPACE color_space = imgformattoJCS(o_format);
        JpegCompresser* compresser = new JpegCompresser;
        if(!compresser->setImage((unsigned char*)outImageBuff->imageData, outImageBuff->width, outImageBuff->height, color_space)){
          std::cout << "compresser error" << std::endl;
	}else{
          compresser->setCompression(aa.compression);
          compresser->compress();
        }
        unsigned char* jpegImage = compresser->getJpegImage();
        unsigned long length = compresser->getJpegImagelen();
        
        // send
        char* message = NULL;
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

  return MHD_NO; }

  case MJPEG_CONNECTION_CLOSE :
    myComp->sendImage = false;
    return MHD_NO;
  case UNKNOWN :
    sendResponse(connection, "invalid command\r\n");
    return MHD_NO;
  default :
    return MHD_NO;
  }     
}

// Module specification
// <rtc-template block="module_spec">
static const char* axis212_spec[] =
  {
    "implementation_id", "Axis212",
    "type_name",         "Axis212",
    "description",       "Axis212 Component",
    "version",           "1.1.0",
    "vendor",            "AIST",
    "category",          "Category",
    "activity_type",     "PERIODIC",
    "kind",              "DataFlowComponent",
    "max_instance",      "1",
    "language",          "C++",
    "lang_type",         "compile",
    "conf.default.httpPort", "8882",
    ""
  };
// </rtc-template>

/*!
 * @brief constructor
 * @param manager Maneger Object
 */
Axis212::Axis212(RTC::Manager* manager)
    // <rtc-template block="initializer">
  : RTC::DataFlowComponentBase(manager),
    m_originalImageIn("originalImage", m_originalImage)

    // </rtc-template>
{
  pantox_map.clear();
  tilttoy_map.clear();
  for(int i=0; i<4; i++)
    pantox_map[pan_key[i]] = x_value[i];
  for(int i=0; i<4; i++)
    tilttoy_map[tilt_key[i]] = y_value[i];
}

/*!
 * @brief destructor
 */
Axis212::~Axis212()
{
}



RTC::ReturnCode_t Axis212::onInitialize()
{
  //std::cout << m_profile.instance_name << ": onInitialize()" << std::endl;
  bindParameter("httpPort", m_port, "8882" );

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
RTC::ReturnCode_t Axis212::onFinalize()
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis212::onStartup(RTC::UniqueId _ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis212::onShutdown(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/


RTC::ReturnCode_t Axis212::onActivated(RTC::UniqueId ec_id)
{
  std::cout << m_profile.instance_name << ": httpPort= " << m_port << std::endl;
  sendImage = false;
  time = -1;
  width = height = 0;
  pan = tilt = 0;
  zoom = 0;
  clip_x = clip_y = 0;
  clip_width = INIMAGEWIDTH;
  clip_height = INIMAGEHEIGHT;  
  horizontalAngle = MAXHORIZONTALANGLE;
  verticalAngle = MAXVERTICALANGLE;
  pixelperHangle = INIMAGEWIDTH / MAXHORIZONTALANGLE;
  pixelperVangle = INIMAGEHEIGHT / MAXVERTICALANGLE;

  pthread_mutex_init(&mutex,NULL);
 
  daemon = MHD_start_daemon (MHD_USE_THREAD_PER_CONNECTION, m_port, NULL, NULL, 
                             &answer_to_connection, this, MHD_OPTION_END);
  if (NULL == daemon) 
    return RTC::RTC_ERROR;

  return RTC::RTC_OK;
}


RTC::ReturnCode_t Axis212::onDeactivated(RTC::UniqueId ec_id)
{
  sendImage = false;
  MHD_stop_daemon(daemon);
  pthread_mutex_destroy(&mutex);
  return RTC::RTC_OK;
}


RTC::ReturnCode_t Axis212::onExecute(RTC::UniqueId ec_id)
{
  //std::cout << m_profile.instance_name << ": onExecute()" << std::endl;
  
  pthread_mutex_lock(&mutex);
  if (m_originalImageIn.isNew()){
    do {
      m_originalImageIn.read();
    }while(m_originalImageIn.isNew());
    if(time < 0){
      clip_width = m_originalImage.data.image.width;
      clip_height = m_originalImage.data.image.height;
    }
    time = m_originalImage.tm.sec + m_originalImage.tm.nsec*1e-9;
    if(width != m_originalImage.data.image.width){
      width = m_originalImage.data.image.width;
      pixelperHangle = (double)width / MAXHORIZONTALANGLE;
    }
    if(height != m_originalImage.data.image.height){
      height = m_originalImage.data.image.height;
      pixelperVangle = (double)height / MAXVERTICALANGLE;
    }
  }  
  pthread_mutex_unlock(&mutex);
  return RTC::RTC_OK;
}

/*
RTC::ReturnCode_t Axis212::onAborting(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis212::onError(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis212::onReset(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis212::onStateUpdate(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t Axis212::onRateChanged(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

double Axis212::getHorizontalAngle()
{
  return horizontalAngle;
}

double Axis212::getVerticalAngle()
{
  return verticalAngle;
}

void Axis212::setZoom(int _zoom)
{
  if(_zoom > MAXZOOM)
    _zoom = MAXZOOM;
  if(_zoom < 0)
    _zoom = 0;
  zoom = _zoom;
  horizontalAngle = MAXHORIZONTALANGLE - (MAXHORIZONTALANGLE - MINIHORIZONTALANGLE)/(double)MAXZOOM*zoom;
  verticalAngle = MAXVERTICALANGLE - (MAXVERTICALANGLE - MINIVERTICALANGLE)/(double)MAXZOOM*zoom;
  //std::cout << "setZoom " << _zoom << " " << zoom << " " << horizontalAngle << " " << verticalAngle << std::endl;
}

void Axis212::setPan(double _pan)
{
  double max = _pan + horizontalAngle/2;
  double min = _pan - horizontalAngle/2;

  if(max > MAXHORIZONTALANGLE/2)
     _pan = (MAXHORIZONTALANGLE - horizontalAngle)/2;
  if(min < -MAXHORIZONTALANGLE/2)
     _pan = -(MAXHORIZONTALANGLE - horizontalAngle)/2;
  pan = _pan;
}

void Axis212::setTilt(double _tilt)
{
  double max = _tilt + verticalAngle/2;
  double min = _tilt - verticalAngle/2;

  if(max > MAXVERTICALANGLE/2)
     _tilt = (MAXVERTICALANGLE - verticalAngle)/2;
  if(min < -MAXVERTICALANGLE/2)
     _tilt =  -(MAXVERTICALANGLE - verticalAngle)/2;
  tilt = _tilt;
}

void Axis212::calcClip_width()
{ 
  clip_width = round(pixelperHangle * horizontalAngle);
}

void Axis212::calcClip_height()
{ 
  clip_height = round(pixelperVangle * verticalAngle);
}

void Axis212::setOriginX(int origin_x)
{
  if( origin_x < 0 )
    origin_x = 0;
  if( origin_x + clip_width > width )
    origin_x = width - clip_width;
  clip_x = origin_x;
  //std::cout << "setOriginX " << clip_x << std::endl; 
}

void Axis212::setOriginY(int origin_y)
{
  if( origin_y < 0 )
    origin_y = 0;
  if( origin_y + clip_height > height )
    origin_y = height - clip_height;
  clip_y = origin_y;
}

void Axis212::setCenterX(int center_x, bool setPan_)
{  
  if(setPan_)
   setPan(center_x/pixelperHangle);
  calcClip_width();
  setOriginX(center_x - clip_width/2);
  //std::cout << "setCenterX " << clip_width << " " << center_x << std::endl;
}

void Axis212::setCenterY(int center_y, bool setTilt_)
{
  if(setTilt_)
    setTilt(center_y/pixelperVangle);
  calcClip_height();
  setOriginY(center_y - clip_height/2);
  //std::cout << "setCenterY " << clip_height << " " << center_y << std::endl;
}

void Axis212::PTZtoClipParam()
{
  setCenterX(round(pixelperHangle * pan)+width/2, false);
  setCenterY(round(pixelperVangle * tilt)+height/2, false);
}


extern "C"
{
 
  void Axis212Init(RTC::Manager* manager)
  {
    coil::Properties profile(axis212_spec);
    manager->registerFactory(profile,
                             RTC::Create<Axis212>,
                             RTC::Delete<Axis212>);
  }
  
};


