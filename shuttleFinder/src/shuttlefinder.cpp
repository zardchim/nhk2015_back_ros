#include <ros/ros.h>

#include <opencv2/opencv.hpp>
#include <cv.h>
#include <highgui.h>
#include <cvblob.h>

#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>

#include <visualization_msgs/Marker.h>	//for displaying points of a shuttle
#include <geometry_msgs/PoseArray.h>	//for publish points of a shuttle

#include <pthread.h>

using namespace cv;
using namespace cvb;

pthread_mutex_t	mutex;  // MUTEX
Mat depth_frame;
ros::Time depth_timestamp;
bool recieved = false;
bool endflag = false;

void thread_main();

void imageCallback(const sensor_msgs::ImageConstPtr& msg)
{
	cv_bridge::CvImagePtr cv_ptr_depth;
	try{
		cv_ptr_depth = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::TYPE_16UC1);
	}

	catch (cv_bridge::Exception& e){
		ROS_ERROR("cv_bridge exception: %s", e.what());
		return;
	}
	pthread_mutex_lock( &mutex );
	depth_frame = cv_ptr_depth->image;
	depth_timestamp = ros::Time::now();
	recieved = true;
	pthread_mutex_unlock( &mutex );
	// convert message from ROS to openCV



}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "image_listener");
  ros::NodeHandle nh;

  cv::startWindowThread();

  image_transport::ImageTransport it(nh);
  image_transport::Subscriber sub = it.subscribe("/kinect2_head/depth/image", 1, imageCallback);

  pthread_t thread;
  pthread_create( &thread, NULL, (void* (*)(void*))thread_main, NULL );

  ros::spin();

  endflag = true;
  pthread_join( thread, NULL );
  destroyAllWindows();

  return 0;
}

float RawDepthToMeters(int depthValue)
{
	return (double)depthValue / 1000;
}
geometry_msgs::Point DepthToWorld(int x, int y, int depthValue)
{
	float fx_d = 0.0027697133333333;
	float fy_d = -0.00271;
	float cx_d = 256;
	float cy_d = 214;

	geometry_msgs::Point result;
	float depth = RawDepthToMeters(depthValue);
	result.y = -(float)((x - cx_d) * depth * fx_d);
	result.z = (float)((y - cy_d) * depth * fy_d);
	result.x = (float)(depth);
	return result;
}

void thread_main(){
	ROS_INFO("New thread Created.");
	pthread_detach( pthread_self( ));

	//namedWindow( "depth_image", WINDOW_AUTOSIZE );
	//namedWindow( "output", WINDOW_AUTOSIZE );
	namedWindow( "frame", WINDOW_AUTOSIZE );

	Mat depthMat8bit;
	Mat depthMask(424, 512,CV_8U);

	cv::BackgroundSubtractorGMG backGroundSubtractor;
	//cv::BackgroundSubtractorMOG backGroundSubtractor;
	//cv::BackgroundSubtractorMOG2 backGroundSubtractor;

	ros::NodeHandle n;
	ros::Publisher marker_pub = n.advertise<visualization_msgs::Marker>("shuttle_marker", 10);
	ROS_INFO("Node shuttle_marker start...");

	ros::Publisher shuttle_pub = n.advertise<geometry_msgs::PoseArray>("shuttle_points", 10);
	ROS_INFO("Node shuttle_points start...");

	CvBlobs blobs;

	while(!endflag){

		visualization_msgs::Marker points;
		points.header.frame_id = "/laser";
		points.ns = "points_and_lines";
		points.action = visualization_msgs::Marker::ADD;
		points.pose.orientation.w = 1.0;

		points.id = 0;
		points.type = visualization_msgs::Marker::POINTS;

		// POINTS markers use x and y scale for width/height respectively
		points.scale.x = 0.1;
		points.scale.y = 0.1;

		// Points are green
		points.color.g = 1.0f;
		points.color.a = 1.0;

		//wait for recieve new frame
		while(recieved == false){
			cv::waitKey(1);
		}

		//Get new frame
		pthread_mutex_lock( &mutex );
		cv::Mat depthMat(depth_frame);
		cv::Mat depthMat8bit(depth_frame);
		ros::Time timestamp = depth_timestamp;
		pthread_mutex_unlock( &mutex );

		points.header.stamp = timestamp;

		geometry_msgs::PoseArray shuttle;
		shuttle.header.stamp = timestamp;
		shuttle.header.frame_id = "laser";


		depthMat8bit.convertTo(depthMat8bit, CV_8U, 255.0 / 8000.0);

		cv::threshold(depthMat8bit, depthMask, 1, 255, cv::THRESH_BINARY_INV);
		depthMask.copyTo(depthMat8bit, depthMask);

		Mat foreGroundMask;
		Mat output;

		backGroundSubtractor(depthMat8bit, foreGroundMask);

		//cv::erode(depthMat8bit, depthMat8bit, cv::Mat() );
		cv::dilate(depthMat8bit, depthMat8bit, cv::Mat());

		// 入力画像にマスク処理を行う
		//cv::bitwise_and(depthMat8bit, depthMat8bit, output, foreGroundMask);

		blobs.clear();

		IplImage dstImg = foreGroundMask;
		IplImage *frame = cvCreateImage(cvGetSize(&dstImg), IPL_DEPTH_8U, 3);
		IplImage *labelImg = cvCreateImage(cvGetSize(&dstImg), IPL_DEPTH_LABEL, 1);
		cvLabel(&dstImg, labelImg, blobs);
		cvFilterByArea(blobs, 20, 10000);

		IplImage iplImage = foreGroundMask;
		cvCvtColor(&iplImage, frame, CV_GRAY2BGR );

		IplImage *imgOut = cvCreateImage(cvGetSize(&dstImg), IPL_DEPTH_8U, 3); cvZero(imgOut);

		cvRenderBlobs(labelImg, blobs, frame, frame, CV_BLOB_RENDER_BOUNDING_BOX);
		//cvUpdateTracks(blobs, tracks, 200., 5);
		//cvRenderTracks(tracks, frame, frame, CV_TRACK_RENDER_ID|CV_TRACK_RENDER_BOUNDING_BOX);

		for (CvBlobs::const_iterator it=blobs.begin(); it!=blobs.end(); ++it){

			//-------------------------------------------------------Detect nearest point
			cv::Rect roi_rect;
			roi_rect.x	= it->second->minx;
			roi_rect.y	= it->second->miny;
			roi_rect.width = it->second->maxx - it->second->minx;
			roi_rect.height = it->second->maxy - it->second->miny;

			cv::Point minPoint(0,0);
			int min = 65535;

			cv::Mat roi(depthMat, roi_rect);

			for (int y = 0; y < roi_rect.height; y++)
			{
				for (int x = 0; x < roi_rect.width; x++)
				{
					int val = roi.at<unsigned short>(y,x);
					//int val = roi.data[y*roi_rect.width + x];
					if (val != 0 && val < min)
					{
						min = val;
						minPoint.x= x;
						minPoint.y = y;
					}
				}
			}
			minPoint.x += roi_rect.x;
			minPoint.y += roi_rect.y;
			//int nearPointIndex = minPoint.x + (minPoint.y) * depthMat.cols;
			geometry_msgs::Point nearest_p = DepthToWorld(minPoint.x, minPoint.y, min);

			//-------------------------------------------------------Check around the point

#define MARGIN_AROUND	30

			cv::Rect aroundRect;
			aroundRect.x		= roi_rect.x-MARGIN_AROUND;
			aroundRect.y		= roi_rect.y-MARGIN_AROUND;
			aroundRect.width	= roi_rect.width+MARGIN_AROUND*2;
			aroundRect.height	= roi_rect.height+MARGIN_AROUND*2;

			if( aroundRect.x < 0)	aroundRect.x = 0;
			if( aroundRect.y < 0)	aroundRect.y = 0;

			if( aroundRect.x + aroundRect.width >=512 )	aroundRect.width = 511 - aroundRect.x;
			if( aroundRect.y + aroundRect.height >=424 )aroundRect.height = 423 - aroundRect.y;

			cv::Mat roi_around(depthMat, aroundRect);

			for (int y = 0; y < aroundRect.height; y++)
			{
				for (int x = 0; x < aroundRect.width; x++)
				{
					int val = roi_around.at<unsigned short>(y,x);
					if (val >= 0 && val < min)
					{
						geometry_msgs::Point p = DepthToWorld(aroundRect.x+x, aroundRect.y+y, val);
						float dist_pow2 = (p.x-nearest_p.x)*(p.x-nearest_p.x) + (p.y-nearest_p.y)*(p.y-nearest_p.y) + (p.z-nearest_p.z)*(p.z-nearest_p.z);
						if( dist_pow2 > 0.30*0.30 && dist_pow2 < 0.65*0.65 ){
							goto not_shuttle;
						}
					}
				}
			}
			//-------------------------------------------------------Draw the point
			cvCircle(frame, minPoint, 10, CV_RGB(238,128,255),3);

			//Now, X = depth, Y = width and Z = height for matching axes of laser scan
			if(nearest_p.x > 1.0f){
				//ROS_INFO("%.4f, %f, %f, %f", timestamp.toSec(), nearest_p.x, nearest_p.y, nearest_p.z, min );
				points.points.push_back(nearest_p);

				geometry_msgs::Pose pose;
				pose.position = nearest_p;
				shuttle.poses.push_back(pose);
			}

			not_shuttle:
			continue;
		}

		cv::imshow("frame", cvarrToMat(frame));

		cvReleaseImage(&labelImg);
		cvReleaseImage(&frame);
		cvReleaseImage(&imgOut);

		//cv::imshow("depth_image", depthMat8bit);
		//cv::imshow("output", output);

		marker_pub.publish(points);
		shuttle_pub.publish(shuttle);
	}
}
