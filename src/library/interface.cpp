#include "orb_slam_2_ros/interface.hpp"

#include <glog/logging.h>
#include <minkindr_conversions/kindr_msg.h>
#include <minkindr_conversions/kindr_tf.h>
#include <opencv2/core/core.hpp>
#include <opencv2/core/eigen.hpp>

#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>

namespace orb_slam_2_interface
{

  OrbSlam2Interface::OrbSlam2Interface(const ros::NodeHandle &nh,
                                       const ros::NodeHandle &nh_private)
      : nh_(nh),
        nh_private_(nh_private),
        verbose_(kDefaultVerbose),
        frame_id_(kDefaultFrameId),
        child_frame_id_(kDefaultChildFrameId)
  {
    // Getting data and params
    advertiseTopics();
    getParametersFromRos();
  }

  void OrbSlam2Interface::advertiseTopics()
  {
    // Advertising topics
    T_pub_ = nh_private_.advertise<geometry_msgs::TransformStamped>(
        "transform_cam", 1);

    pose_pub_ = nh_private_.advertise<geometry_msgs::PoseStamped>(
        "pose_cam", 1);
    cloud_pub_ = nh_private_.advertise<sensor_msgs::PointCloud2>("keypoints_cloud", 1);

    // Creating a callback timer for TF publisher
    tf_timer_ = nh_.createTimer(ros::Duration(0.01),
                                &OrbSlam2Interface::publishCurrentPoseAsTF, this);
  }

  void OrbSlam2Interface::getParametersFromRos()
  {
    // Getting the paths to the files required by orb slam
    CHECK(nh_private_.getParam("vocabulary_file_path", vocabulary_file_path_))
        << "Please provide the vocabulary_file_path as a ros param.";
    CHECK(nh_private_.getParam("settings_file_path", settings_file_path_))
        << "Please provide the settings_file_path as a ros param.";
    // Optional params
    nh_private_.getParam("verbose", verbose_);
    nh_private_.getParam("frame_id", frame_id_);
    nh_private_.getParam("child_frame_id", child_frame_id_);
  }

  void OrbSlam2Interface::publishCurrentPose(const Transformation &T,
                                             const std_msgs::Header &header)
  {
    // Creating the message
    geometry_msgs::TransformStamped msg;
    // Filling out the header
    msg.header = header;
    // Setting the child and parent frames
    msg.child_frame_id = child_frame_id_;
    // Converting from a minkindr transform to a transform message
    tf::transformKindrToMsg(T, &msg.transform);
    // Publishing the current transformation.
    T_pub_.publish(msg);
  }

  void OrbSlam2Interface::publishCurrentPoseAsPose(const Transformation &T,
                                                   const std_msgs::Header &header)
  {
    // Creating the message
    geometry_msgs::PoseStamped msg;
    // Filling out the header
    msg.header = header;
    msg.header.frame_id = "world";
    // Converting from a minkindr transform to a pose message
    tf::poseKindrToMsg(T, &msg.pose);
    // Publishing the current pose.
    pose_pub_.publish(msg);
  }

  void OrbSlam2Interface::publishCurrentPoseAsTF(const ros::TimerEvent &event)
  {
    tf::Transform tf_transform;
    tf::transformKindrToTF(T_W_C_, &tf_transform);
    tf_broadcaster_.sendTransform(tf::StampedTransform(
        tf_transform, ros::Time::now(), frame_id_, child_frame_id_));
  }

  void OrbSlam2Interface::publishPointCloud(const std::vector<ORB_SLAM2::MapPoint *> &mapPoints, const std_msgs::Header &header)
  {
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    pcl::PointXYZRGB point;

    for (const auto &mp : mapPoints)
    {
      if (mp == nullptr)
      {
        continue;
      }
      cv::Mat worldPos = mp->GetWorldPos();
      if (worldPos.empty())
      {
        continue;
      }
      point.x = worldPos.at<float>(0);
      point.y = worldPos.at<float>(1);
      point.z = worldPos.at<float>(2);
      point.r = 255;
      point.g = 255;
      point.b = 255;
      cloud->points.push_back(point);
    }

    sensor_msgs::PointCloud2 cloudMsg;
    cloudMsg.header = header;
    pcl::toROSMsg(*cloud, cloudMsg);
    cloudMsg.header.frame_id = "world";

    cloud_pub_.publish(cloudMsg);
  }

  void OrbSlam2Interface::convertOrbSlamPoseToKindr(const cv::Mat &T_cv,
                                                    Transformation *T_kindr)
  {
    // Argument checks
    CHECK_NOTNULL(T_kindr);
    CHECK_EQ(T_cv.cols, 4);
    CHECK_EQ(T_cv.rows, 4);
    // Open CV mat to Eigen matrix (float)
    Eigen::Matrix4f T_eigen_f;
    cv::cv2eigen(T_cv, T_eigen_f);
    // Eigen matrix (float) to Eigen matrix (double)
    Eigen::Matrix4d T_eigen_d = T_eigen_f.cast<double>();
    // Extracting and orthonormalizing the rotation matrix
    Eigen::Matrix3d R_unnormalized = T_eigen_d.block<3, 3>(0, 0);
    Eigen::AngleAxisd aa(R_unnormalized);
    Eigen::Matrix3d R = aa.toRotationMatrix();
    // Constructing the transformation
    Quaternion q_kindr(R);
    Eigen::Vector3d t_kindr(T_eigen_d.block<3, 1>(0, 3));
    *T_kindr = Transformation(q_kindr, t_kindr);
  }

} // namespace orb_slam_2_interface
