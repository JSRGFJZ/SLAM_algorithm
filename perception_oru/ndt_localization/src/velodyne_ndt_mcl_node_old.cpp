//perception_oru
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include "ndt_mcl/particle_filter.hpp"
#include "ndt_map/ndt_map.h"
#include "ndt_map/ndt_conversions.h"
//pcl
#include <pcl/conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl_ros/transforms.h>
//ros
#include "geometry_msgs/PoseArray.h"
#include "laser_geometry/laser_geometry.h"
#include "sensor_msgs/LaserScan.h"
#include "sensor_msgs/PointCloud2.h"
#include "nav_msgs/Odometry.h"
#include "ros/ros.h"
#include "ros/rate.h"
#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>

#include <velodyne_pointcloud/rawdata.h>
#include <velodyne_pointcloud/point_types.h>

//std
#include <Eigen/Dense>
#include <string>
#include <map>
#include <fstream>
#include <iostream>
#include <chrono>

class particle_filter_wrap {
    ros::NodeHandle nh;
    //Map parameters
    std::string mapFile;
    lslgeneric::NDTMap* ndtMap;
    lslgeneric::LazyGrid* mapGrid;
    //MCL
    bool be2D;
    bool forceSIR;
    bool showParticles;
    bool showPose;
    double resolution;
    // std::map<std::string,lslgeneric::init_type> inits;
    std::string initType;
    lslgeneric::particle_filter* MCL;
    int particleCount;
    ros::Publisher mclPosePub;
    //laser input
    std::string veloTopicName;  //std::string laserTopicName;
    ros::Publisher particlePub;
    ros::Subscriber laserSub;
    ros::Subscriber initPoseSub;
    bool useLaser;
    laser_geometry::LaserProjection laserProjector;
    double sensorOffsetX;
    double sensorOffsetY;
    double sensorOffsetTh;
    std::string rootTF;
    std::string odomTF;
    Eigen::Affine3d tOld;
    bool firstLoad;
    geometry_msgs::PoseArray parMsg;
    double minx;
    double miny;
    bool informed;
    bool gauss;
    bool uniform;
    bool ndt;
    bool grid;
    bool histogram;
    double initX;
    double initY;
    double initVar;
    int counter;
    int counterLimit;
    bool flip;
    message_filters::Subscriber<sensor_msgs::LaserScan> *laserSubSync;
    //  double particlePeriod;//DEBUG
    std::ofstream res;
    std::string resF_name;
    bool initialized;
    double time_0;
    geometry_msgs::Pose initial_pose;

    nav_msgs::Odometry Pose2DToMsg(Eigen::Vector3d mean, Eigen::Matrix3d cov, ros::Time ts, Eigen::Affine3d Todometry) {
        nav_msgs::Odometry O;
        static int seq = 0;
        //	O.header.stamp = ts;
        //O.header.seq = seq;
        O.header.frame_id = rootTF;
        O.child_frame_id = "/mcl_pose";

        O.pose.pose.position.x = mean[0];
        O.pose.pose.position.y = mean[1];
        tf::Quaternion q;
        q.setRPY(0, 0, mean[2]);
        O.pose.pose.orientation.x = q.getX();
        O.pose.pose.orientation.y = q.getY();
        O.pose.pose.orientation.z = q.getZ();
        O.pose.pose.orientation.w = q.getW();

        O.pose.covariance[0] = cov(0, 0);
        O.pose.covariance[1] = cov(0, 1);
        O.pose.covariance[2] = 0;
        O.pose.covariance[3] = 0;
        O.pose.covariance[4] = 0;
        O.pose.covariance[5] = 0;

        O.pose.covariance[6] = cov(1, 0);
        O.pose.covariance[7] = cov(1, 1);
        O.pose.covariance[8] = 0;
        O.pose.covariance[9] = 0;
        O.pose.covariance[10] = 0;
        O.pose.covariance[11] = 0;

        O.pose.covariance[12] = 0;
        O.pose.covariance[13] = 0;
        O.pose.covariance[14] = 0;
        O.pose.covariance[15] = 0;
        O.pose.covariance[16] = 0;
        O.pose.covariance[17] = 0;

        O.pose.covariance[18] = 0;
        O.pose.covariance[19] = 0;
        O.pose.covariance[20] = 0;
        O.pose.covariance[21] = 0;
        O.pose.covariance[22] = 0;
        O.pose.covariance[23] = 0;

        O.pose.covariance[24] = 0;
        O.pose.covariance[25] = 0;
        O.pose.covariance[26] = 0;
        O.pose.covariance[27] = 0;
        O.pose.covariance[28] = 0;
        O.pose.covariance[29] = 0;

        O.pose.covariance[30] = 0;
        O.pose.covariance[31] = 0;
        O.pose.covariance[32] = 0;
        O.pose.covariance[33] = 0;
        O.pose.covariance[34] = 0;
        O.pose.covariance[35] = cov(2, 2);

        seq++;
        //    ROS_INFO("publishing tf");
        static tf::TransformBroadcaster br, br_mapOdom;
        tf::Transform transform;
        transform.setOrigin( tf::Vector3(mean[0], mean[1], 0.0) );
        transform.setRotation( q );
        //br.sendTransform(tf::StampedTransform(transform, ts, "world", "mcl_pose"));
        br.sendTransform(tf::StampedTransform(transform, ts, "map", "mcl_pose"));
        ///Compute TF between map and odometry frame
        Eigen::Affine3d Tmcl = getAsAffine(mean[0], mean[1], mean[2]);
        Eigen::Affine3d Tmap_odo = Tmcl * Todometry.inverse();
        tf::Transform tf_map_odo;
        tf_map_odo.setOrigin( tf::Vector3(Tmap_odo.translation()(0), Tmap_odo.translation()(1), 0.0) );
        tf::Quaternion q_map_odo;
        q_map_odo.setRPY( 0, 0, Tmap_odo.rotation().eulerAngles(0, 1, 2)(2) );
        tf_map_odo.setRotation( q_map_odo );
        // /// broadcast TF
        br_mapOdom.sendTransform(tf::StampedTransform(tf_map_odo, ts, "/map_n", "/odom"));

        return O;
    }


    geometry_msgs::PoseArray ParticlesToMsg(std::vector<lslgeneric::particle> particles) {
        ROS_INFO("publishing particles");
        geometry_msgs::PoseArray ret;
        for (int i = 0; i < particles.size(); i++) {
            geometry_msgs::Pose temp;
            double x, y, z, r, p, t;
            particles[i].GetXYZ(x, y, z);
            particles[i].GetRPY(r, p, t);
            temp.position.x = x;
            temp.position.y = y;
            temp.position.z = z;
            temp.orientation = tf::createQuaternionMsgFromRollPitchYaw(r, p, t);
            ret.poses.push_back(temp);
        }
        ret.header.frame_id = rootTF;
        return ret;
    }

    int LoadMap() {
        //FILE * jffin;
        //jffin = fopen(mapFile.c_str(),"r+b");
        mapGrid = new lslgeneric::LazyGrid(resolution);
        ndtMap = new lslgeneric::NDTMap(mapGrid);
        if (ndtMap->loadFromJFF(mapFile.c_str()) < 0) {
            return -1;
        }
        return 0;
    }


    lslgeneric::NDTMap velodyneToNDT//(velodyne_msgs::VelodyneScan scan)
    (const sensor_msgs::PointCloud2::ConstPtr& scan) {
        pcl::PCLPointCloud2 pcl_pc2;
        pcl_conversions::toPCL(*scan, pcl_pc2);
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
        pcl::fromPCLPointCloud2(pcl_pc2, *cloud);
        //    pcl::PointCloud<pcl::PointXYZ>::Ptr temp_cloud(new pcl::PointCloud<pcl::PointXYZ>);
        //pcl::fromPCLPointCloud2(pcl_pc2,*temp_cloud);

        // pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
        // //cloud->reserve(100000);
        // //std::default_random_engine generator;
        // //std::normal_distribution<double> distribution(0.0, 0.01);
        // //ROS_INFO_STREAM(scan->packets.size());
        // velodyne_rawdata::RawData dataParser;
        // for(size_t next = 0; next < scan.packets.size(); ++next){
        //   velodyne_rawdata::VPointCloud pnts;
        //   dataParser.unpack(scan.packets[next], pnts);                                                                                                                                                                                                                                                                                                                                                                                  // unpack the raw data
        //   // ROS_INFO_STREAM(pnts.size());

        //   //#pragma omp parallel for
        //   for(size_t i = 0; i < pnts.size(); i++){
        //     pcl::PointXYZ p;
        //     p.x = pnts.points[i].x;
        //     p.y = pnts.points[i].y;
        //     p.z = pnts.points[i].z;
        //     // if(sqrt(p.x*p.x+p.y*p.y+p.z*p.z)<80)
        //     cloud->push_back(p);
        //   }
        //   pnts.clear();
        // }

        // int x_or;                                                                                                                                                                                                   //*< number of cells along x axis
        // int y_or;                                                                                                                                                                                                   //*< number of cells along y axis
        // int z_or;                                                                                                                                                                                                   //*< number of cells along z axis
        // ROS_INFO_STREAM(cloud->size());

        double sensorOffsetX = 0.695;
        double sensorOffsetY = -0.01;
        double sensorOffsetZ = -0.01;
        double sensorOffsetTh = -0.0069813;

        double cxs, cys, czs;
        Eigen::Affine3f transform_2 = Eigen::Affine3f::Identity();
        transform_2.rotate(Eigen::AngleAxisf(sensorOffsetTh, Eigen::Vector3f::UnitZ()));                                                                                                                ////////////////////

        transform_2.translation() << sensorOffsetX, sensorOffsetY, sensorOffsetZ;
        pcl::PointCloud<pcl::PointXYZ>::Ptr transformed_cloud(new pcl::PointCloud<pcl::PointXYZ>);
        pcl::transformPointCloud(*cloud, *transformed_cloud, transform_2);
        //lslgeneric::NDTMap *localMap = new lslgeneric::NDTMap(new lslgeneric::LazyGrid(resolution));
        lslgeneric::NDTMap localMap(new lslgeneric::LazyGrid(resolution));
        double v_size_x = 200.0;
        double v_size_y = 200.0;
        double v_size_z = 6.0;
        localMap.guessSize(0, 0, 0, v_size_x, v_size_y, v_size_z);
        localMap.loadPointCloud(*transformed_cloud, v_size_x);
        localMap.computeNDTCells(CELL_UPDATE_MODE_SAMPLE_VARIANCE);

        return localMap;
    }


    lslgeneric::NDTMap laserToNDT(const sensor_msgs::LaserScan::ConstPtr& scan) {
        double cxs, cys, czs;
        minx = std::numeric_limits<double>::max();
        miny = std::numeric_limits<double>::max();
        mapGrid->getCellSize(cxs, cys, czs);

        //laserProjector.projectLaser(*msg,tempCloud);
        //pcl::fromROSMsg(tempCloud,tempCloudPCL);

        ///Calculate the laser pose with respect to the base
        float dy = sensorOffsetY;
        float dx = sensorOffsetX;
        float alpha = atan2(dy, dx);
        float L = sqrt(dx * dx + dy * dy);

        ///Laser pose in base frame
        float lpx = L * cos(alpha);
        float lpy = L * sin(alpha);
        float lpa = sensorOffsetTh;

        ///Laser scan to PointCloud expressed in the base frame
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
        if (!flip) {
            int N = (scan->angle_max - scan->angle_min) / scan->angle_increment;

            for (int j = 0; j < N; j++) {
                double r  = scan->ranges[j];
                if ( r >= scan->range_min && scan->range_max > r ) { /*&& r>0.3 && r<20.0)*/ ///WTF?
                    double a  = scan->angle_min + j * scan->angle_increment;
                    pcl::PointXYZ pt;
                    pt.x = r * cos(a + lpa) + lpx;
                    pt.y = r * sin(a + lpa) + lpy;
                    pt.z = 0.1 + 0.02 * (double)rand() / (double)RAND_MAX;
                    cloud->push_back(pt);
                }
            }
        } else {
            int N = (scan->angle_max - scan->angle_min) / scan->angle_increment;
            //      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
            for (int j = 0; j < N; j++) {
                double r  = scan->ranges[j];
                if ( r >= scan->range_min && scan->range_max > r ) { /*&& r>0.3 && r<20.0)*/ ///WTF?
                    double a  = scan->angle_max - j * scan->angle_increment;
                    pcl::PointXYZ pt;
                    pt.x = r * cos(a + lpa) + lpx;
                    pt.y = r * sin(a + lpa) + lpy;
                    pt.z = 0.1 + 0.02 * (double)rand() / (double)RAND_MAX;
                    cloud->push_back(pt);
                }
            }
        }

        if (firstLoad && (informed || ndt)) {
            for (int i = 0; i < cloud->points.size(); i++) {
                if (cloud->points[i].x < minx)
                    minx = cloud->points[i].x;
                if (cloud->points[i].y < miny)
                    miny = cloud->points[i].y;
            }
        }
        lslgeneric::NDTMap localMap(new lslgeneric::LazyGrid(cxs));
        localMap.addPointCloudSimple(*cloud);
        localMap.computeNDTCells(CELL_UPDATE_MODE_STUDENT_T);
        return localMap;
    }

    void laserCallback(const sensor_msgs::LaserScan::ConstPtr& msg) {
        //    ROS_INFO("LASER recived");
        sensor_msgs::PointCloud2 tempCloud;
        pcl::PointCloud<pcl::PointXYZ> tempCloudPCL;
        static tf::TransformListener tf_listener;
        tf::StampedTransform transform;
        double x, y, yaw;

        if (!initialized) {
            initialized = true;
            MCL->InitializeNormal(initial_pose.position.x, initial_pose.position.y, 0.3); //tf::getYaw(initial_pose.orientation));
            //MCL->InitializeNormal(initial_pose.position.x,initial_pose.position.y,tf::getYaw(initial_pose.orientation),0.3);//tf::getYaw(initial_pose.orientation));
            ROS_INFO("Initialized");
        }
        //ROS_INFO_STREAM("test 1 "<<msg->header.stamp);
//    tf_listener.waitForTransform(odomTF, "/base_link" , msg->header.stamp,ros::Duration(1.0));
        tf_listener.waitForTransform(odomTF, "/base_link", msg->header.stamp, ros::Duration(0.1));
        //ROS_INFO("test 2");
        try {
            tf_listener.lookupTransform(odomTF, "/base_link", msg->header.stamp, transform);
            yaw = tf::getYaw(transform.getRotation());
            x = transform.getOrigin().x();
            y = transform.getOrigin().y();
        } catch (tf::TransformException ex) {
            ROS_ERROR("%s", ex.what());
            return;
        }
        Eigen::Affine3d T = getAsAffine(x, y, yaw);
        if (firstLoad) {
            tOld = T;
            firstLoad = false;

        }
        Eigen::Affine3d tMotion = tOld.inverse() * T;
        tOld = T;
        MCL->UpdateAndPredict(tMotion, laserToNDT(msg));
        //if(showParticles)
        //particlePub.publish(ParticlesToMsg(MCL->particleCloud));
        Eigen::Matrix3d cov;
        Eigen::Vector3d mean;
        nav_msgs::Odometry mcl_pose;
        //ROS_INFO("publishing pose");
        MCL->GetPoseMeanAndVariance2D(mean, cov);
        mcl_pose = Pose2DToMsg(mean, cov, msg->header.stamp, T);
        //if(showPose){
        mcl_pose.header.stamp = msg->header.stamp;
        //mclPosePub.publish(mcl_pose);
        //}
    }



    void veloCallback(const sensor_msgs::PointCloud2::ConstPtr& msg) {
        ROS_INFO("VELO recived");
        sensor_msgs::PointCloud2 tempCloud;
        pcl::PointCloud<pcl::PointXYZ> tempCloudPCL;
        static tf::TransformListener tf_listener;
        tf::StampedTransform transform;
        double x, y, yaw;

        if (!initialized) {
            initialized = true;
            MCL->InitializeNormal(initial_pose.position.x, initial_pose.position.y, 0.3); //tf::getYaw(initial_pose.orientation));
            //MCL->InitializeNormal(initial_pose.position.x,initial_pose.position.y,tf::getYaw(initial_pose.orientation),0.3);//tf::getYaw(initial_pose.orientation));
            ROS_INFO("Initialized");
        }
        //ROS_INFO_STREAM("test 1 "<<msg->header.stamp);
        //    tf_listener.waitForTransform(odomTF, "/base_link" , msg->header.stamp,ros::Duration(1.0));
        tf_listener.waitForTransform(odomTF, "/base_link", msg->header.stamp, ros::Duration(0.1));
        //ROS_INFO("test 2");
        try {
            tf_listener.lookupTransform(odomTF, "/base_link", msg->header.stamp, transform);
            yaw = tf::getYaw(transform.getRotation());
            x = transform.getOrigin().x();
            y = transform.getOrigin().y();
        } catch (tf::TransformException ex) {
            ROS_ERROR("%s", ex.what());
            return;
        }
        Eigen::Affine3d T = getAsAffine(x, y, yaw);
        if (firstLoad) {
            tOld = T;
            firstLoad = false;

        }
        Eigen::Affine3d tMotion = tOld.inverse() * T;
        tOld = T;
        MCL->UpdateAndPredict(tMotion, velodyneToNDT(msg));
        //if(showParticles)
        //particlePub.publish(ParticlesToMsg(MCL->particleCloud));
        Eigen::Matrix3d cov;
        Eigen::Vector3d mean;
        nav_msgs::Odometry mcl_pose;
        //ROS_INFO("publishing pose");
        MCL->GetPoseMeanAndVariance2D(mean, cov);
        mcl_pose = Pose2DToMsg(mean, cov, msg->header.stamp, T);
        //if(showPose){
        mcl_pose.header.stamp = msg->header.stamp;
        //mclPosePub.publish(mcl_pose);
        //}
    }


    void initialposeCallback(geometry_msgs::PoseWithCovarianceStamped input_init) {
        initialized = false;
        initial_pose = input_init.pose.pose;
        firstLoad = true;
    }
    Eigen::Affine3d getAsAffine(float x, float y, float yaw ) {
        Eigen::Matrix3d m;
        m = Eigen::AngleAxisd(0, Eigen::Vector3d::UnitX())
            * Eigen::AngleAxisd(0, Eigen::Vector3d::UnitY())
            * Eigen::AngleAxisd(yaw, Eigen::Vector3d::UnitZ());
        Eigen::Translation3d v(x, y, 0);
        Eigen::Affine3d T = v * m;
        return T;
    }

public:
    particle_filter_wrap(ros::NodeHandle param) {
        param.param<std::string>("map_file", mapFile, "file.jff");
        param.param<double>("resolution", resolution, 0.2);
        param.param<bool>("be2D", be2D, true);
        param.param<bool>("force_SIR", forceSIR, true);
        param.param<bool>("show_particles", showParticles, false);
        param.param<bool>("show_pose", showPose, true);
        param.param<int>("particle_count", particleCount, 500);
        //param.param<std::string>("laser_topic_name",laserTopicName,"/spencer/sensors/laser_front/echo0");
        param.param<std::string>("velo_topic_name", veloTopicName, "/velodyne_points");
        param.param<std::string>("root_tf", rootTF, "/map_n");
        param.param<std::string>("odom_tf", odomTF, "/odom");
        param.param<double>("sensor_pose_x", sensorOffsetX, 0.14);
        param.param<double>("sensor_pose_y", sensorOffsetY, 0.0);
        param.param<double>("sensor_pose_th", sensorOffsetTh, 0.0);
        param.param<double>("initial_x", initX, 10.0);
        param.param<double>("initial_y", initY, 2);
        param.param<double>("init_var", initVar, 0.5);
        param.param<bool>("flip", flip, false);
        /////////
        firstLoad = true;
        initialized = false;
        res.open(resF_name);
        std::cout << "velodyne_ndt_mcl_node will load map\n";
        if (LoadMap() < 0) {
            std::cout << "velodyne_ndt_mcl_node load map failed; shutting down\n";
            ros::shutdown();
        }
        std::cout << "velodyne_ndt_mcl_node load map OK\n";
        MCL = new lslgeneric::particle_filter(ndtMap, particleCount, be2D, forceSIR);
        //laserSub=nh.subscribe(laserTopicName,1,&particle_filter_wrap::laserCallback,this);
        laserSub = nh.subscribe(veloTopicName, 1, &particle_filter_wrap::veloCallback, this);
        initPoseSub = nh.subscribe("/initialpose", 1000, &particle_filter_wrap::initialposeCallback, this);
        if (showParticles)
            particlePub = nh.advertise<geometry_msgs::PoseArray>("particles", 1);
        //if(showPose)
        //mclPosePub=nh.advertise<nav_msgs::Odometry>("ndt_mcl_pose", 1);
        std::cout << "velodyne_ndt_mcl_node will now enter spin()\n";
        ros::spin();
    }
};
int main(int argc, char **argv) {
    ros::init(argc, argv, "ndt_mcl");
    ros::NodeHandle parameters("~");
    particle_filter_wrap pf(parameters);
}
