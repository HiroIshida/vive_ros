
#include <cmath>

#include <ros/ros.h>
#include <std_srvs/Empty.h>
#include <sensor_msgs/Joy.h>
#include <sensor_msgs/JoyFeedback.h>
#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>

#include <opencv2/imgcodecs.hpp>
#include "vive_ros/vr_interface.h"
#include "vive_ros/hellovr_opengl_main.h"

void handleDebugMessages(const std::string &msg) {ROS_DEBUG(" [VIVE] %s",msg.c_str());}
void handleInfoMessages(const std::string &msg) {ROS_INFO(" [VIVE] %s",msg.c_str());}
void handleErrorMessages(const std::string &msg) {ROS_ERROR(" [VIVE] %s",msg.c_str());}

#include <image_transport/image_transport.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <cv_bridge/cv_bridge.h>

enum {X, Y, XY};
enum {L, R, LR};

class CMainApplicationMod : public CMainApplication{
  public:
    CMainApplicationMod( int argc, char *argv[] )
    : CMainApplication( argc, argv )
    , hmd_fov(110*M_PI/180) {
      for(int i=0;i<LR;i++){
        cam_f[i][X] = cam_f[i][Y] = 600;
      }
      RenderFrame_hz_count = 0;
    };
    ~CMainApplicationMod(){};
    VRInterface* vr_p;

    cv::Mat ros_img[LR];
    double cam_f[LR][XY];
    const double hmd_fov;//field of view
    float hmd_fov_h, hmd_fov_v;
    int RenderFrame_hz_count;

    void InitTextures(){
      ros_img[L] = cv::Mat(cv::Size(m_nRenderWidth, m_nRenderHeight), CV_8UC3, CV_RGB(0, 0, 0));
      ros_img[R] = cv::Mat(cv::Size(m_nRenderWidth, m_nRenderHeight), CV_8UC3, CV_RGB(0, 0, 0));
      hmd_panel_img[L] = cv::Mat(cv::Size(m_nRenderWidth, m_nRenderHeight), CV_8UC3, CV_RGB(0, 0, 0));
      hmd_panel_img[R] = cv::Mat(cv::Size(m_nRenderWidth, m_nRenderHeight), CV_8UC3, CV_RGB(0, 0, 0));
      for ( int i = 0; i < vr::k_unMaxTrackedDeviceCount; i++){
        if(m_pHMD->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_HMD){
          m_pHMD->GetStringTrackedDeviceProperty( i, vr::Prop_ScreenshotHorizontalFieldOfViewDegrees_Float, (char *)&hmd_fov_h, sizeof(float), NULL );
          m_pHMD->GetStringTrackedDeviceProperty( i, vr::Prop_ScreenshotVerticalFieldOfViewDegrees_Float, (char *)&hmd_fov_v, sizeof(float), NULL );
        }
      }
    }
    void RenderFrame(){
      ros::Time tmp = ros::Time::now();
      if ( m_pHMD ){
        RenderControllerAxes();
        RenderStereoTargets();
        UpdateTexturemaps();
        RenderCompanionWindow();
        vr::Texture_t leftEyeTexture = {(void*)(uintptr_t)leftEyeDesc.m_nResolveTextureId, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
        vr::Texture_t rightEyeTexture = {(void*)(uintptr_t)rightEyeDesc.m_nResolveTextureId, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
        vr::VRCompositor()->Submit(vr::Eye_Left, &leftEyeTexture );
        vr::VRCompositor()->Submit(vr::Eye_Right, &rightEyeTexture );
      }
      if ( m_bVblank && m_bGlFinishHack ){ glFinish(); }
      SDL_GL_SwapWindow( m_pCompanionWindow );
      glClearColor( 0, 0, 0, 1 );
      glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
      if ( m_bVblank ){
        glFlush();
        glFinish();
      }

      if ( m_iTrackedControllerCount != m_iTrackedControllerCount_Last || m_iValidPoseCount != m_iValidPoseCount_Last ){
        m_iValidPoseCount_Last = m_iValidPoseCount;
        m_iTrackedControllerCount_Last = m_iTrackedControllerCount;
      }
      UpdateHMDMatrixPose();
      RenderFrame_hz_count++;
    }

  private:
    cv::Mat hmd_panel_img[LR];
    cv::Mat ros_img_resized[LR];
    

    void processROSStereoImage(cv::Mat (&in)[LR], cv::Mat (&out)[LR]){
      const double hmd_eye2panel_z[XY] = { (double)out[L].cols/2/tan(hmd_fov/2), (double)out[L].rows/2/tan(hmd_fov/2) };
      const double cam_pic_size[LR][XY] = { { (double)in[L].cols, (double)in[L].rows }, { (double)in[R].cols, (double)in[R].rows } };
      double cam_fov[LR][XY];
      int cam_pic_size_on_hmd[LR][XY];
      cv::Mat hmd_panel_roi[LR];
    
      for(int i=0;i<LR;i++){
        ROS_INFO_THROTTLE(3.0,"Process ROS image[%d] (%dx%d) with fov (%dx%d) to (%dx%d)", i, in[i].cols, in[i].rows, (int)cam_f[i][X], (int)cam_f[i][Y], out[i].cols, out[i].rows);
        for(int j=0;j<XY;j++){
          cam_fov[i][j] = 2 * atan( cam_pic_size[i][j]/2 / cam_f[i][j] );
          cam_pic_size_on_hmd[i][j] = (int)( hmd_eye2panel_z[j] * 2 * tan(cam_fov[i][j]/2) );
        }
        cv::resize(in[i], ros_img_resized[i], cv::Size(cam_pic_size_on_hmd[i][X], cam_pic_size_on_hmd[i][Y]));
        cv::flip(ros_img_resized[i], ros_img_resized[i], 0);
        cv::Rect hmd_panel_area_rect( ros_img_resized[i].cols/2-out[i].cols/2, ros_img_resized[i].rows/2-out[i].rows/2, out[i].cols, out[i].rows);
        cv::Rect ros_img_resized_rect( 0, 0, ros_img_resized[i].cols, ros_img_resized[i].rows);
        cv::Point ros_img_resized_center(ros_img_resized[i].cols/2, ros_img_resized[i].rows/2);
        cv::Rect cropped_rect;
        if( !hmd_panel_area_rect.contains( cv::Point(ros_img_resized_rect.x, ros_img_resized_rect.y) )
            || !hmd_panel_area_rect.contains( cv::Point(ros_img_resized_rect.x+ros_img_resized_rect.width,ros_img_resized_rect.y+ros_img_resized_rect.height) ) ){
          cropped_rect = ros_img_resized_rect & hmd_panel_area_rect;
          ros_img_resized[i] = ros_img_resized[i](cropped_rect);
        }
        cv::Rect hmd_panel_draw_rect( cropped_rect.x-hmd_panel_area_rect.x, cropped_rect.y-hmd_panel_area_rect.y, ros_img_resized[i].cols, ros_img_resized[i].rows);
        ros_img_resized[i].copyTo(out[i](hmd_panel_draw_rect));
      }
    }


    


    void UpdateTexturemaps(){
      processROSStereoImage(ros_img, hmd_panel_img);
      for(int i=0;i<LR;i++){
        if(i==L)glBindTexture( GL_TEXTURE_2D, leftEyeDesc.m_nResolveTextureId );
        else if(i==R)glBindTexture( GL_TEXTURE_2D, rightEyeDesc.m_nResolveTextureId );
        else break;
        int cur_tex_w,cur_tex_h;
        glGetTexLevelParameteriv( GL_TEXTURE_2D , 0 , GL_TEXTURE_WIDTH , &cur_tex_w );
        glGetTexLevelParameteriv( GL_TEXTURE_2D , 0 , GL_TEXTURE_HEIGHT , &cur_tex_h );
        glTexSubImage2D( GL_TEXTURE_2D, 0, cur_tex_w/2 - hmd_panel_img[i].cols/2, cur_tex_h/2 - hmd_panel_img[i].rows/2, hmd_panel_img[i].cols, hmd_panel_img[i].rows, GL_RGB, GL_UNSIGNED_BYTE, hmd_panel_img[i].data );
        glBindTexture( GL_TEXTURE_2D, 0 );
      }
    }
};

cv::Mat translateImg(cv::Mat &img, int offsetx, int offsety){
      cv::Mat trans_mat = (cv::Mat_<double>(2,3) << 1, 0, offsetx, 0, 1, offsety);
      cv::warpAffine(img,img,trans_mat,img.size());
      return img;
    }

class VIVEnode
{
  public:
    VIVEnode(int rate);
    ~VIVEnode();
    bool Init();
    void Run();
    void Shutdown();
    bool setOriginCB(std_srvs::Empty::Request& req, std_srvs::Empty::Response& res);
    ros::NodeHandle nh_;
    VRInterface vr_;

    void imageCb_L(const sensor_msgs::CompressedImageConstPtr& msg);
    void imageCb_R(const sensor_msgs::CompressedImageConstPtr& msg);
    void infoCb_L(const sensor_msgs::CameraInfoConstPtr& msg);
    void infoCb_R(const sensor_msgs::CameraInfoConstPtr& msg);
    CMainApplicationMod *pMainApplication;
    ros::Subscriber sub_L,sub_R;
    ros::Subscriber sub_i_L,sub_i_R;

  private:
    ros::Rate loop_rate_;
    std::vector<double> world_offset_;
    double world_yaw_;
    tf::TransformBroadcaster tf_broadcaster_;
    tf::TransformListener tf_listener_;
    ros::ServiceServer set_origin_server_;
    std::map<std::string, ros::Publisher> button_states_pubs_map;
    ros::Subscriber feedback_sub_;

};

VIVEnode::VIVEnode(int rate)
  : loop_rate_(rate)
  , nh_("~")
  , tf_broadcaster_()
  , tf_listener_()
  , vr_()
  , world_offset_({0, 0, 0})
  , world_yaw_(0)
{
  nh_.getParam("/vive/world_offset", world_offset_);
  nh_.getParam("/vive/world_yaw", world_yaw_);
  set_origin_server_ = nh_.advertiseService("/vive/set_origin", &VIVEnode::setOriginCB, this);

  image_transport::ImageTransport it(nh_);
  sub_L = nh_.subscribe("image_left", 1, &VIVEnode::imageCb_L, this, ros::TransportHints().udp());
  sub_R = nh_.subscribe("image_right", 1, &VIVEnode::imageCb_R, this, ros::TransportHints().udp());
  sub_i_L = nh_.subscribe("camera_info_left", 1, &VIVEnode::infoCb_L, this, ros::TransportHints().udp());
  sub_i_R = nh_.subscribe("camera_info_right", 1, &VIVEnode::infoCb_R, this, ros::TransportHints().udp());
  
  pMainApplication = new CMainApplicationMod( 1, NULL );
  if (!pMainApplication->BInit()){
    pMainApplication->Shutdown();
    Shutdown();
  }
  pMainApplication->vr_p = &(vr_);
  pMainApplication->InitTextures();

  return;
}

VIVEnode::~VIVEnode()
{
  return;
}

bool VIVEnode::Init()
{
  //  Set logging functions
  vr_.setDebugMsgCallback(handleDebugMessages);
  vr_.setInfoMsgCallback(handleInfoMessages);
  vr_.setErrorMsgCallback(handleErrorMessages);

  if (!vr_.Init())
  {
    return false;
  }

  return true;
}

void VIVEnode::Shutdown()
{
  vr_.Shutdown();
}

bool VIVEnode::setOriginCB(std_srvs::Empty::Request& req, std_srvs::Empty::Response& res)
{
  double tf_matrix[3][4];
  int index = 1, dev_type;
  while (dev_type != 2) 
  {
    dev_type = vr_.GetDeviceMatrix(index++, tf_matrix);
  }
  if (dev_type == 0) 
  {
    ROS_WARN(" [VIVE] Coulnd't find controller 1.");
    return false;
  }

  tf::Matrix3x3 rot_matrix(tf_matrix[0][0], tf_matrix[0][1], tf_matrix[0][2],
                           tf_matrix[1][0], tf_matrix[1][1], tf_matrix[1][2],
                           tf_matrix[2][0], tf_matrix[2][1], tf_matrix[2][2]);
  tf::Vector3 c_z;
  c_z = rot_matrix*tf::Vector3(0,0,1);
  c_z[1] = 0;
  c_z.normalize();
  double new_yaw = acos(tf::Vector3(0,0,1).dot(c_z)) + M_PI_2;
  if (c_z[0] < 0) new_yaw = -new_yaw;
  world_yaw_ = -new_yaw;

  tf::Vector3 new_offset;
  tf::Matrix3x3 new_rot;
  new_rot.setRPY(0, 0, world_yaw_);
  new_offset = new_rot*tf::Vector3(-tf_matrix[0][3], tf_matrix[2][3], -tf_matrix[1][3]);

  world_offset_[0] = new_offset[0];
  world_offset_[1] = new_offset[1];
  world_offset_[2] = new_offset[2];

  nh_.setParam("/vive/world_offset", world_offset_);
  nh_.setParam("/vive/world_yaw", world_yaw_);

  return true;
}


void VIVEnode::Run()
{
  double tf_matrix[3][4];
  int run_hz_count = 0;

  while (ros::ok())
  {
    // do stuff
    vr_.Update();

    for (int i=0; i<vr::k_unMaxTrackedDeviceCount; i++)
    {
      int dev_type = vr_.GetDeviceMatrix(i, tf_matrix);

      // No device
      if (dev_type == 0) continue;
      
      pMainApplication->HandleInput();
      pMainApplication->RenderFrame();

      ROS_INFO_THROTTLE(1.0,"HMD frequency @ %d [fps]", [](int& cin){int ans = cin; cin=0; return ans;}(run_hz_count));
      run_hz_count++;
      ros::spinOnce();
      loop_rate_.sleep();
    }
  }
}

void VIVEnode::imageCb_L(const sensor_msgs::CompressedImageConstPtr& msg){
  if(true){
    try {
      pMainApplication->ros_img[L] = cv::imdecode(cv::Mat(msg->data),1);//convert compressed image data to cv::Mat
      translateImg(pMainApplication->ros_img[L],100,0);
    } catch (cv_bridge::Exception& e) {
    }
  }else{
  }
}
void VIVEnode::imageCb_R(const sensor_msgs::CompressedImageConstPtr& msg){
  if(true){
    try {
      pMainApplication->ros_img[R] = cv::imdecode(cv::Mat(msg->data),1);//convert compressed image data to cv::Mat
      translateImg(pMainApplication->ros_img[R],-100,0);
    } catch (cv_bridge::Exception& e) {
    }
  }else{
  }
}
void VIVEnode::infoCb_L(const sensor_msgs::CameraInfoConstPtr& msg){
  if(msg->K[0] > 0.0 && msg->K[4] > 0.0 ){

    pMainApplication->cam_f[L][0] = msg->K[0];
    pMainApplication->cam_f[L][1] = msg->K[4];
  }else{
    ROS_WARN_THROTTLE(3, "Invalid camera_info_left fov (%fx%f) use default", msg->K[0], msg->K[4]);
  }
}
void VIVEnode::infoCb_R(const sensor_msgs::CameraInfoConstPtr& msg){
  if(msg->K[0] > 0.0 && msg->K[4] > 0.0 ){ 

    pMainApplication->cam_f[R][0] = msg->K[0];
    pMainApplication->cam_f[R][1] = msg->K[4];
  }else{
    ROS_WARN_THROTTLE(3, "Invalid camera_info_right fov (%fx%f) use default", msg->K[0], msg->K[4]);
  }
}


// Main
int main(int argc, char** argv){
  ros::init(argc, argv, "vive_node");

  VIVEnode nodeApp(90); // VIVE display max fps

  if (!nodeApp.Init()){
    nodeApp.Shutdown();
    return 1;
  }

  nodeApp.Run();
  nodeApp.Shutdown();

  return 0;
};
