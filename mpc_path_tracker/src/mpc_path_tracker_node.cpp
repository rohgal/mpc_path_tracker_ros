#include <iostream>
#include <map>
#include <math.h>

#include "ros/ros.h"
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/Twist.h>
#include <tf/transform_listener.h>
#include <std_msgs/Float32.h>

#include <nav_msgs/Path.h>
#include <nav_msgs/Odometry.h>
#include <plan2control_msgs/PathSpeed.h>

#include "mpc_path_tracker.h"
#include <Eigen/Core>
#include <Eigen/QR>

#include <iostream>
#include <fstream>
#include <string>

using namespace std;
using namespace Eigen;


class MPCNode
{
    public:
        MPCNode();
        ~MPCNode();
        int get_thread_numbers();
        
    private:
        ros::NodeHandle _nh;
        ros::Subscriber _sub_odom, _sub_desired_path;
        ros::Publisher _pub_totalcost, _pub_ctecost, _pub_ethetacost,_pub_odompath, _pub_twist, _pub_mpctraj;
        ros::Timer _timer1;
        tf::TransformListener _tf_listener;

        geometry_msgs::Point _goal_pos;
        nav_msgs::Odometry _odom;
        nav_msgs::Path _odom_path, _mpc_traj; 
        geometry_msgs::Twist _cur_vel, _twist_msg;

        string _desired_path_topic;
        string _odom_frame, _car_frame;

        MPC _mpc;
        map<string, double> _mpc_params;
        double _mpc_steps, _ref_cte, _ref_etheta, _ref_vel, _w_cte, _w_etheta, _w_vel, 
               _w_angvel, _w_accel, _w_angvel_d, _w_accel_d, _max_angvel, _max_throttle, _bound_value;

        double _dt, _w, _throttle, _speed, _max_speed;
        double _pathLength, _goalRadius, _waypointsDist;
        int _controller_freq, _downSampling, _thread_numbers;
        bool _goal_received, _goal_reached, _path_computed, _pub_twist_flag, _debug_info, _delay_mode;

        double polyeval(Eigen::VectorXd coeffs, double x);
        Eigen::VectorXd polyfit(Eigen::VectorXd xvals, Eigen::VectorXd yvals, int order);

        void odomCB(const nav_msgs::Odometry::ConstPtr& odomMsg);
        void desiredPathCB(const plan2control_msgs::PathSpeed::ConstPtr& totalPathSpeedMsg);
        void goalCB(const geometry_msgs::PoseStamped::ConstPtr& goalMsg);
        void controlLoopCB(const ros::TimerEvent&);

        //For making global planner
        nav_msgs::Path _gen_path;
        unsigned int min_idx;
        
        double _mpc_etheta;
        double _mpc_cte;
        fstream file;
        unsigned int idx;
}; // end of class

MPCNode::MPCNode()
{
    //Private parameters handler
    ros::NodeHandle pn("~");

    //Parameters for control loop
    pn.param("thread_numbers", _thread_numbers, 2); // number of threads for this ROS node
    pn.param("pub_twist_cmd", _pub_twist_flag, true);
    pn.param("debug_info", _debug_info, true);
    pn.param("delay_mode", _delay_mode, true);
    pn.param("max_speed", _max_speed, 0.50); // unit: m/s
    pn.param("waypoints_dist", _waypointsDist, -1.0); // unit: m
    pn.param("goal_radius", _goalRadius, 0.5); // unit: m
    pn.param("controller_freq", _controller_freq, 10);
    _dt = double(1.0/_controller_freq); // time step duration dt in s 

    //Parameter for MPC solver
    pn.param("mpc_steps", _mpc_steps, 20.0);
    pn.param("mpc_ref_cte", _ref_cte, 0.0);
    pn.param("mpc_ref_etheta", _ref_etheta, 0.0);
    pn.param("mpc_w_cte", _w_cte, 5000.0);
    pn.param("mpc_w_etheta", _w_etheta, 5000.0);
    pn.param("mpc_w_vel", _w_vel, 1.0);
    pn.param("mpc_w_angvel", _w_angvel, 100.0);
    pn.param("mpc_w_angvel_d", _w_angvel_d, 10.0);
    pn.param("mpc_w_accel", _w_accel, 50.0);
    pn.param("mpc_w_accel_d", _w_accel_d, 10.0);
    pn.param("mpc_max_angvel", _max_angvel, 3.0); // Maximal angvel radian (~30 deg)
    pn.param("mpc_max_throttle", _max_throttle, 1.0); // Maximal throttle accel
    pn.param("mpc_bound_value", _bound_value, 1.0e3); // Bound value for other variables

    //Parameter for topics & Frame name
    pn.param<std::string>("reference_path_speed_topic", _desired_path_topic, "/reference_path_speed" );
    pn.param<std::string>("odom_frame", _odom_frame, "burger/odom");
    pn.param<std::string>("car_frame", _car_frame, "burger/base_footprint" );

    //Display the parameters
    cout << "\n===== Parameters =====" << endl;
    cout << "pub_twist_cmd: "  << _pub_twist_flag << endl;
    cout << "debug_info: "  << _debug_info << endl;
    cout << "delay_mode: "  << _delay_mode << endl;
    cout << "frequency: "   << _dt << endl;
    cout << "mpc_steps: "   << _mpc_steps << endl;
    cout << "mpc_w_cte: "   << _w_cte << endl;
    cout << "mpc_w_etheta: "  << _w_etheta << endl;

    //Publishers and Subscribers
    _sub_odom  = _nh.subscribe("/odom", 1, &MPCNode::odomCB, this);
    _sub_desired_path  = _nh.subscribe(_desired_path_topic, 10, &MPCNode::desiredPathCB, this);
    
    _pub_odompath = _nh.advertise<nav_msgs::Path>("/mpc_reference", 1); // reference path for MPC ///mpc_reference 
    _pub_mpctraj = _nh.advertise<nav_msgs::Path>("/mpc_trajectory", 1);// MPC trajectory output

    if(_pub_twist_flag)
        _pub_twist = _nh.advertise<geometry_msgs::Twist>("/cmd_vel", 1); //for stage (Ackermann msg non-supported)
    
    _pub_totalcost = _nh.advertise<std_msgs::Float32>("/total_cost", 1); // Global path generated from another source
    _pub_ctecost = _nh.advertise<std_msgs::Float32>("/cross_track_error", 1); // Global path generated from another source
    _pub_ethetacost = _nh.advertise<std_msgs::Float32>("/theta_error", 1); // Global path generated from another source
    
    //Timer
    _timer1 = _nh.createTimer(ros::Duration((1.0)/_controller_freq), &MPCNode::controlLoopCB, this); // 10Hz //*****mpc

    //Init variables
    _goal_received = false;
    _goal_reached  = false;
    _path_computed = false;
    _throttle = 0.0; 
    _w = 0.0;
    _speed = 0.0;

    _twist_msg = geometry_msgs::Twist();
    _mpc_traj = nav_msgs::Path();

    //Init parameters for MPC object
    _mpc_params["DT"] = _dt;
    _mpc_params["STEPS"]    = _mpc_steps;
    _mpc_params["REF_CTE"]  = _ref_cte;
    _mpc_params["REF_ETHETA"] = _ref_etheta;
    _mpc_params["REF_V"]    = _ref_vel;
    _mpc_params["W_CTE"]    = _w_cte;
    _mpc_params["W_EPSI"]   = _w_etheta;
    _mpc_params["W_V"]      = _w_vel;
    _mpc_params["W_ANGVEL"]  = _w_angvel;
    _mpc_params["W_A"]      = _w_accel;
    _mpc_params["W_DANGVEL"] = _w_angvel_d;
    _mpc_params["W_DA"]     = _w_accel_d;
    _mpc_params["ANGVEL"]   = _max_angvel;
    _mpc_params["MAXTHR"]   = _max_throttle;
    _mpc_params["BOUND"]    = _bound_value;
    _mpc.LoadParams(_mpc_params);

    min_idx = 0;
    idx = 0;
    _mpc_etheta = 0;
    _mpc_cte = 0;
}

MPCNode::~MPCNode()
{
    // file.close();
};

// Public: return _thread_numbers
int MPCNode::get_thread_numbers()
{
    return _thread_numbers;
}

// Evaluate a polynomial.
double MPCNode::polyeval(Eigen::VectorXd coeffs, double x) 
{
    double result = 0.0;
    for (int i = 0; i < coeffs.size(); i++) 
    {
        result += coeffs[i] * pow(x, i);
    }
    return result;
}

// Fit a polynomial.
Eigen::VectorXd MPCNode::polyfit(Eigen::VectorXd xvals, Eigen::VectorXd yvals, int order) 
{
    assert(xvals.size() == yvals.size());
    assert(order >= 1 && order <= xvals.size() - 1);
    Eigen::MatrixXd A(xvals.size(), order + 1);

    for (int i = 0; i < xvals.size(); i++)
        A(i, 0) = 1.0;

    for (int j = 0; j < xvals.size(); j++) 
    {
        for (int i = 0; i < order; i++) 
            A(j, i + 1) = A(j, i) * xvals(j);
    }

    auto Q = A.householderQr();
    auto result = Q.solve(yvals);
    return result;
}

// CallBack: Update odometry
void MPCNode::odomCB(const nav_msgs::Odometry::ConstPtr& odomMsg)
{
    _odom = *odomMsg;

    if(_goal_received)
    {
        double car2goal_x = _goal_pos.x - odomMsg->pose.pose.position.x;
        double car2goal_y = _goal_pos.y - odomMsg->pose.pose.position.y;
        double dist2goal = sqrt(car2goal_x*car2goal_x + car2goal_y*car2goal_y);
        if(dist2goal < _goalRadius)
        {
            _goal_received = false;
            _goal_reached = true;
            _path_computed = false;
            ROS_INFO("Goal Reached !");
        }
    }
}

// CallBack: Update generated path (conversion to odom frame)
void MPCNode::desiredPathCB(const plan2control_msgs::PathSpeed::ConstPtr& totalPathSpeedMsg)
{
    _goal_received = true;
    _goal_reached = false;
    nav_msgs::Path path_globalframe = nav_msgs::Path();
    nav_msgs::Path mpc_path = nav_msgs::Path();  
    // path_vis.header.frame_id = "map";
    geometry_msgs::PoseStamped tempPose;
    nav_msgs::Odometry odom = _odom;
    int N = totalPathSpeedMsg->path.poses.size();

    for(int i = 0; i < N; i++)
    {
        _tf_listener.transformPose(_odom_frame, ros::Time(0) , 
                                        totalPathSpeedMsg->path.poses[i], _odom_frame, tempPose);                     
        path_globalframe.poses.push_back(tempPose);
    }

    try
    {                  
        // Find the nearst point for robot position
        int min_val = 100; 
    
        const double px = odom.pose.pose.position.x; //pose: odom frame
        const double py = odom.pose.pose.position.y;
        
        double dx, dy; // difference distance
        double pre_yaw = 0;
        double roll, pitch, yaw = 0;

        // Get a goal point
        _goal_pos.x = path_globalframe.poses[N-1].pose.position.x;
        _goal_pos.y = path_globalframe.poses[N-1].pose.position.y;

        for(int i = min_idx; i < N; i++) 
        {
            dx = path_globalframe.poses[i].pose.position.x - px;
            dy = path_globalframe.poses[i].pose.position.y - py;
                    
            tf::Quaternion q(
                path_globalframe.poses[i].pose.orientation.x,
                path_globalframe.poses[i].pose.orientation.y,
                path_globalframe.poses[i].pose.orientation.z,
                path_globalframe.poses[i].pose.orientation.w);
            tf::Matrix3x3 m(q);
            m.getRPY(roll, pitch, yaw);

            if(abs(pre_yaw - yaw) > 5)
            {
                cout << "abs(pre_yaw - yaw)" << abs(pre_yaw - yaw) << endl;
                pre_yaw = yaw;
            }
       
            if(min_val > sqrt(dx*dx + dy*dy) && abs((int)(i - min_idx)) < 50)
            {
                min_val = sqrt(dx*dx + dy*dy);
                min_idx = i;
            }
        }

        for(int i = min_idx; i < N ; i++)
        {
            mpc_path.poses.push_back(path_globalframe.poses[i]);                                    
        }   

        _odom_path = mpc_path; // Path waypoints in odom frame
        _ref_vel = totalPathSpeedMsg->speed;
        _mpc_params["REF_V"] = _ref_vel;
        _mpc.LoadParams(_mpc_params);
        _path_computed = true;

        // publish odom path
        mpc_path.header.frame_id = "map";
        mpc_path.header.stamp = ros::Time::now();
        _pub_odompath.publish(mpc_path);      
    }
    catch(tf::TransformException &ex)
    {
        ROS_ERROR("%s",ex.what());
        ros::Duration(1.0).sleep();
    }
}

// Timer: Control Loop (closed loop nonlinear MPC)
void MPCNode::controlLoopCB(const ros::TimerEvent&)
{
    if(_goal_received && !_goal_reached && _path_computed ) //received goal & goal not reached    
    {
        nav_msgs::Odometry odom = _odom;
        geometry_msgs::Twist cur_vel = _cur_vel;
        nav_msgs::Path odom_path = _odom_path;   

        // Update system states: X=[x, y, theta, v]
        const double px = odom.pose.pose.position.x; //pose: odom frame
        const double py = odom.pose.pose.position.y;
        tf::Pose pose;
        tf::poseMsgToTF(odom.pose.pose, pose);
        const double theta = tf::getYaw(pose.getRotation());
        const double v = cur_vel.linear.x; //twist: body fixed frame

        // Update system inputs: U=[w, throttle]
        const double w = _w; // steering -> w
        const double throttle = _throttle; // accel: >0; brake: <0
        const double dt = _dt;

        // Waypoints related parameters
        const int N = odom_path.poses.size(); // Number of waypoints
        const double costheta = cos(theta);
        const double sintheta = sin(theta);

        // Convert to the vehicle coordinate system
        VectorXd x_veh(N);
        VectorXd y_veh(N);
        for(int i = 0; i < N; i++) 
        {
            const double dx = odom_path.poses[i].pose.position.x - px;
            const double dy = odom_path.poses[i].pose.position.y - py;
            x_veh[i] = dx * costheta + dy * sintheta;
            y_veh[i] = dy * costheta - dx * sintheta;
        }
        
        // Fit waypoints
        auto coeffs = polyfit(x_veh, y_veh, 3); 

        const double cte  = polyeval(coeffs, 0.0);
        const double etheta = atan(coeffs[1]);

        _mpc_cte = cte;
        _mpc_etheta = etheta;

        VectorXd state(6);
        if(_delay_mode)
        {
            // Kinematic model is used to predict vehicle state at the actual moment of control (current time + delay dt)
            const double px_act = v * dt;
            const double py_act = 0;
            const double theta_act = w * dt; //(steering) theta_act = v * steering * dt / Lf;
            const double v_act = v + throttle * dt; //v = v + a * dt
            
            const double cte_act = cte + v * sin(etheta) * dt;
            const double etheta_act = etheta - theta_act;  
            
            state << px_act, py_act, theta_act, v_act, cte_act, etheta_act;
        }
        else
        {
            state << 0, 0, 0, v, cte, etheta;
        }
        
        // Solve MPC Problem
        vector<double> mpc_results = _mpc.Solve(state, coeffs);
              
        // MPC result (all described in car frame), output = (acceleration, w)        
        _w = mpc_results[0]; // radian/sec, angular velocity
        _throttle = mpc_results[1]; // acceleration
        _speed = v + _throttle*dt;  // speed

        _cur_vel.linear.x = _speed;

        if (_speed >= _max_speed)
            _speed = _max_speed;
            
        if(_speed <= 0.0)
            _speed = 0.0;

        if(_debug_info)
        {
            cout << "\n\nDEBUG" << endl;
            cout << "theta: " << theta << endl;
            cout << "V: " << v << endl;
            //cout << "odom_path: \n" << odom_path << endl;
            //cout << "x_points: \n" << x_veh << endl;
            //cout << "y_points: \n" << y_veh << endl;
            cout << "coeffs: \n" << coeffs << endl;
            cout << "_w: \n" << _w << endl;
            cout << "_throttle: \n" << _throttle << endl;
            cout << "_speed: \n" << _speed << endl;
        }
    }
    else
    {
        _throttle = 0.0;
        _speed = 0.0;
        _w = 0;
        if(_goal_reached && _goal_received)
            cout << "Goal Reached: control loop !" << endl;
    }

    // publish general cmd_vel 
    if(_pub_twist_flag)
    {
        _twist_msg.linear.x  = _speed; 
        _twist_msg.angular.z = _w;
        _pub_twist.publish(_twist_msg);

        std_msgs::Float32 mpc_total_cost;
        mpc_total_cost.data = static_cast<float>(_mpc._mpc_totalcost);
        _pub_totalcost.publish(mpc_total_cost);

        std_msgs::Float32 mpc_cte_cost;
        mpc_cte_cost.data = static_cast<float>(_mpc._mpc_ctecost);
        _pub_ctecost.publish(mpc_cte_cost);

        std_msgs::Float32 mpc_etheta_cost;
        mpc_etheta_cost.data = static_cast<float>(_mpc._mpc_ethetacost);
        _pub_ethetacost.publish(mpc_etheta_cost);
    }
    else
    {
        _twist_msg.linear.x  = 0; 
        _twist_msg.angular.z = 0;
        _pub_twist.publish(_twist_msg);
    }
}

int main(int argc, char **argv)
{
    //Initiate ROS
    ros::init(argc, argv, "MPC_Node");
    MPCNode mpc_node;
    ROS_INFO("Waiting for path msgs ~");
    ros::AsyncSpinner spinner(mpc_node.get_thread_numbers()); // Use multi threads
    spinner.start();
    ros::waitForShutdown();
    return 0;
}
