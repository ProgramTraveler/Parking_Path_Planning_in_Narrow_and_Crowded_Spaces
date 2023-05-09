#include "planning/planning_method_flow.h"

#include "nav_msgs/Path.h"
#include "visualization_msgs/Marker.h"
#include "visualization_msgs/MarkerArray.h"
#include "tf/transform_datatypes.h"
#include "tf/transform_broadcaster.h"


PlanningMethodFlow::PlanningMethodFlow(ros::NodeHandle &nh) { // 初始化
    double steering_angle = nh.param("planner/steering_angle", 10); // 转向角
    int steering_angle_discrete_num = nh.param("planner/steering_angle_discrete_num", 1); // 转向角离散数

    double wheel_base = nh.param("planner/wheel_base", 1.0); // 轴距

    double segment_length = nh.param("planner/segment_length", 1.6); // 段长
    int segment_length_discrete_num = nh.param("planner/segment_length_discrete_num", 8); // 段长离散数

    double steering_penalty = nh.param("planner/steering_penalty", 1.5); // 转向惩罚
    double steering_change_penalty = nh.param("planner/steering_change_penalty", 2.0); // 转向变化惩罚
    
    double reversing_penalty = nh.param("planner/reversing_penalty", 2.0); // 倒车惩罚

    double shot_distance = nh.param("planner/shot_distance", 5.0); // 

    kinodynamic_searcher_ptr_ = std::make_shared<PlanningMethod>
                                (steering_angle, steering_angle_discrete_num, segment_length, segment_length_discrete_num, wheel_base,
                                steering_penalty, reversing_penalty, steering_change_penalty, shot_distance);

    costmap_sub_ptr_ = std::make_shared<CostMapSubscriber>(nh, "/map", 1);

    // 创建一个 InitPoseSubscriber2D 对象(是一个智能指针类型) 
    // 传入的三个参数分别为 ros::NodeHandle 对象 话题名称 队列大小
    init_pose_sub_ptr_ = std::make_shared<InitPoseSubscriber2D>(nh, "/initialpose", 1);

    goal_pose_sub_ptr_ = std::make_shared<GoalPoseSubscriber2D>(nh, "/move_base_simple/goal", 1);

    has_map_ = false;
}

void PlanningMethodFlow::Run() {
    ReadData();

    if (!has_map_) { // 读入地图信息
        if (costmap_deque_.empty()) return ;

        current_costmap_ptr_ = costmap_deque_.front();
        costmap_deque_.pop_front();

        const double map_resolution = 0.2; // 地图分辨率

        kinodynamic_searcher_ptr_ -> Init(
            current_costmap_ptr_ -> info.origin.position.x, // x_lower
            1.0 * current_costmap_ptr_ -> info.width * current_costmap_ptr_ -> info.resolution, // x_upper
            current_costmap_ptr_ -> info.origin.position.y, // y_lower
            1.0 * current_costmap_ptr_ -> info.height * current_costmap_ptr_ -> info.resolution, // y_upper
            current_costmap_ptr_ -> info.resolution, // state_grid_resolution
            map_resolution // map_grid_resolution
        );

        unsigned int map_w = std::floor(current_costmap_ptr_ -> info.width / map_resolution);
        unsigned int map_h = std::floor(current_costmap_ptr_ -> info.height / map_resolution);

        for (unsigned int w = 0; w < map_w; w ++) {
            for (unsigned int h = 0; h < map_h; h ++) {
                auto x = static_cast<unsigned int>((w + 0.5) * map_resolution / current_costmap_ptr_ -> info.resolution);
                auto y = static_cast<unsigned int>((h + 0.5) * map_resolution / current_costmap_ptr_ -> info.resolution);

                if (current_costmap_ptr_ -> data[y * current_costmap_ptr_ -> info.width + x]) {
                    kinodynamic_searcher_ptr_ -> SetObstacle(w, h);
                }
            }
        }

        has_map_ = true;

    }

    costmap_deque_.clear();
    
    while (HasStartPose() && HasGoalPose()) { // 初始位姿和目标位姿队列不为空
        // 获取当前位姿信息
        InitPoseData();

        // 从四元数中提取偏航角
        double start_yaw = tf::getYaw(current_init_pose_ptr_ -> pose.pose.orientation);
        double goal_yaw = tf::getYaw(current_goal_pose_ptr_ -> pose.orientation);

        Vec3d start_state = Vec3d(
            current_init_pose_ptr_ -> pose.pose.position.x,
            current_init_pose_ptr_ -> pose.pose.position.y,
            start_yaw
        );

        Vec3d goal_state = Vec3d(
            current_goal_pose_ptr_ -> pose.position.x,
            current_goal_pose_ptr_ -> pose.position.y,
            goal_yaw
        );
        if (kinodynamic_searcher_ptr_ -> Search(start_state, goal_state)) {

        }
    }
}

void PlanningMethodFlow::ReadData() {
    init_pose_sub_ptr_ -> ParseData(init_pose_deque_); // 获取初始位置信息 -> init_pose_deque_
    goal_pose_sub_ptr_ -> ParseData(goal_pose_deque_); // 获取目标位置信息 -> goal_pose_deque_

    costmap_sub_ptr_ -> ParseData(costmap_deque_);
}

void PlanningMethodFlow::InitPoseData() {
    current_init_pose_ptr_ = init_pose_deque_.front();
    init_pose_deque_.pop_front();

    current_goal_pose_ptr_ = goal_pose_deque_.front();
    goal_pose_deque_.pop_front();
}

bool PlanningMethodFlow::HasStartPose() {
    return !init_pose_deque_.empty();
}

bool PlanningMethodFlow::HasGoalPose() {
    return !goal_pose_deque_.empty();
}