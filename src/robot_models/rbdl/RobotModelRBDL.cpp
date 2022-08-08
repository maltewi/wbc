#include "RobotModelRBDL.hpp"
#include "tools/URDFTools.hpp"
#include <rbdl/rbdl_utils.h>
#include <rbdl/addons/urdfreader/urdfreader.h>
#include <base-logging/Logging.hpp>

using namespace RigidBodyDynamics;

namespace wbc {

RobotModelRegistry<RobotModelRBDL> RobotModelRBDL::reg("rbdl");

RobotModelRBDL::RobotModelRBDL(){

}

RobotModelRBDL::~RobotModelRBDL(){

}

void RobotModelRBDL::clear(){
    rbdl_model.reset();
    rbdl_model = std::make_shared<Model>();
}

void RobotModelRBDL::updateFloatingBase(const base::samples::RigidBodyStateSE3& floating_base_state_in){

}

bool RobotModelRBDL::configure(const RobotModelConfig& cfg){

    clear();

    // 1. Load Robot Model

    robot_model_config = cfg;
    robot_urdf = urdf::parseURDFFile(cfg.file);
    if(!robot_urdf){
        LOG_ERROR("Unable to parse urdf model from file %s", cfg.file.c_str());
        return false;
    }
    base_frame = robot_urdf->getRoot()->name;

    // Add floating base
    joint_names = URDFTools::jointNamesFromURDF(robot_urdf);
    has_floating_base = cfg.floating_base;
    world_frame = base_frame;
    if(cfg.floating_base){
        joint_names_floating_base = URDFTools::addFloatingBaseToURDF(robot_urdf);
        world_frame = robot_urdf->getRoot()->name;
    }

    if(!Addons::URDFReadFromFile(cfg.file.c_str(), rbdl_model.get(), cfg.floating_base)){
        LOG_ERROR_S << "Unable to parse urdf from file " << cfg.file << std::endl;
        return false;
    }

    actuated_joint_names = joint_names;
    joint_names = independent_joint_names = joint_names_floating_base + joint_names;

    // Read Joint Limits
    URDFTools::jointLimitsFromURDF(robot_urdf, joint_limits);    

    // 3. Create data structures

    joint_state.names = actuated_joint_names;
    joint_state.elements.resize(actuated_joint_names.size());

    // Fixed base: If the robot has N dof, q_size = qd_size = N
    // Floating base: If the robot has N dof, q_size = N+7, qd_size = N+6
    q.resize(rbdl_model->q_size);
    qd.resize(rbdl_model->qdot_size);
    qdd.resize(rbdl_model->qdot_size);

    selection_matrix.resize(noOfActuatedJoints(),noOfJoints());
    selection_matrix.setZero();

    return true;
}

void RobotModelRBDL::update(const base::samples::Joints& joint_state_in,
                            const base::samples::RigidBodyStateSE3& floating_base_state_in){

    if(joint_state_in.elements.size() != joint_state_in.names.size()){
        LOG_ERROR_S << "Size of names and size of elements in joint state do not match"<<std::endl;
        throw std::runtime_error("Invalid joint state");
    }

    if(joint_state_in.time.isNull()){
        LOG_ERROR_S << "Joint State does not have a valid timestamp. Or do we have 1970?"<<std::endl;
        throw std::runtime_error("Invalid joint state");
    }

    joint_state = joint_state_in;

    uint start_idx = 0;
    if(has_floating_base){
        floating_base_state = floating_base_state_in;
        start_idx = 6;

        // Transformation from fb body linear acceleration to fb joint linear acceleration:
        // since RBDL treats the floating base as XYZ translation followed by spherical
        // aj is S*qdd(i)
        // j is parent of i
        // a(i) = Xa(j) + aj + cross(v(i), vj)
        // For pinocchio we give directly a(fb), for RBDL we give aj instead (for the first two joints)
        // so we have to remove the cross contribution cross(v(i), vj) from it
        Eigen::Matrix3d fb_rot = floating_base_state.pose.orientation.toRotationMatrix();
        base::Twist fb_twist = floating_base_state.twist;
        base::Acceleration fb_acc = floating_base_state.acceleration;

        Eigen::VectorXd spherical_j_vel(6);
        spherical_j_vel << fb_twist.angular, Eigen::Vector3d::Zero();
        Eigen::VectorXd spherical_b_vel(6);
        spherical_b_vel << fb_twist.angular, fb_rot.transpose() * fb_twist.linear;

        Eigen::VectorXd fb_spherical_cross = Math::crossm(spherical_b_vel, spherical_j_vel);
        // remove cross contribution from linear acc s(in world coordinates as RBDL want)
        fb_acc.linear = fb_acc.linear - fb_rot * fb_spherical_cross.tail<3>();

        int floating_body_id = rbdl_model->GetBodyId(base_frame.c_str());
        rbdl_model->SetQuaternion(floating_body_id, Math::Quaternion(floating_base_state_in.pose.orientation.coeffs()), q);

        for(int i = 0; i < 3; i++){
            q[i] = floating_base_state.pose.position[i];
            qd[i] = fb_twist.linear[i];
            qdd[i] = fb_acc.linear[i];
            qd[i+3] = fb_twist.angular[i];
            qdd[i+3] = fb_acc.angular[i];
        }
    }

    for(int i = 0; i < actuated_joint_names.size(); i++){
        const std::string &name = actuated_joint_names[i];
        if(!hasJoint(name)){
            LOG_ERROR_S << "Joint " << name << " is a non-fixed joint in the KDL Tree, but it is not in the joint state vector."
                        << "You should either set the joint to 'fixed' in your URDF file or provide a valid joint state for it" << std::endl;
            throw std::runtime_error("Incomplete Joint State");
        }
        const base::JointState& state = joint_state_in[name];
        q[i+start_idx] = state.position;
        qd[i+start_idx] = state.speed;
        qdd[i+start_idx] = state.acceleration;
    }
    joint_state = joint_state_in;
}

void RobotModelRBDL::systemState(base::VectorXd &_q, base::VectorXd &_qd, base::VectorXd &_qdd){
    _q = q;
    _qd = qd;
    _qdd = qdd;
}

const base::samples::RigidBodyStateSE3 &RobotModelRBDL::rigidBodyState(const std::string &root_frame, const std::string &tip_frame){
    if(joint_state.time.isNull()){
        LOG_ERROR("You have to call update() with appropriately timestamped joint data at least once before requesting kinematic information!");
        throw std::runtime_error(" Invalid call to rigidBodyState()");
    }
    if(!hasLink(tip_frame)){
        LOG_ERROR_S << "Request rigidBodyState for " << root_frame << " -> " << tip_frame << " but link " << tip_frame << " does not exist in robot model" << std::endl;
        throw std::runtime_error("Invalid call to rigidBodyState()");
    }
    if(root_frame != world_frame){
        LOG_ERROR_S<<"Requested Forward kinematics computation for kinematic chain "<<root_frame<<"->"<<tip_frame<<" but hyrodyn robot model always requires the root frame to be the root of the full model"<<std::endl;
        throw std::runtime_error("Invalid call to rigidBodyState()");
    }

    int body_id = rbdl_model->GetBodyId(tip_frame.c_str());
    rbs.pose.position = CalcBodyToBaseCoordinates(*rbdl_model,q,body_id,base::Vector3d(0,0,0));
    rbs.pose.orientation = base::Quaterniond(CalcBodyWorldOrientation(*rbdl_model,q,body_id).inverse());
    Math::SpatialVector twist_rbdl = CalcPointVelocity6D(*rbdl_model,q,qd,body_id,base::Vector3d(0,0,0));
    Math::SpatialVector acc_rbdl = CalcPointAcceleration6D(*rbdl_model,q,qd,qdd,body_id,base::Vector3d(0,0,0));
    rbs.twist.linear = twist_rbdl.segment(3,3);
    rbs.twist.angular = twist_rbdl.segment(0,3);
    rbs.acceleration.linear = acc_rbdl.segment(3,3);
    rbs.acceleration.angular = acc_rbdl.segment(0,3);
    rbs.frame_id = root_frame;
    rbs.time = joint_state.time;

    return rbs;
}

const base::MatrixXd &RobotModelRBDL::spaceJacobian(const std::string &root_frame, const std::string &tip_frame){
    if(joint_state.time.isNull()){
        LOG_ERROR("You have to call update() with appropriately timestamped joint data at least once before requesting kinematic information!");
        throw std::runtime_error(" Invalid call to spaceJacobian()");
    }
    if(!hasLink(tip_frame)){
        LOG_ERROR_S << "Request jacobian for " << root_frame << " -> " << tip_frame << " but link " << tip_frame << " does not exist in robot model" << std::endl;
        throw std::runtime_error("Invalid call to spaceJacobian()");
    }
    if(root_frame != world_frame){
        LOG_ERROR_S<<"Requested Forward kinematics computation for kinematic chain "<<root_frame<<"->"<<tip_frame<<" but hyrodyn robot model always requires the root frame to be the root of the full model"<<std::endl;
        throw std::runtime_error("Invalid call to spaceJacobian()");
    }

    std::string chain_id = chainID(root_frame,tip_frame);

    uint nj = rbdl_model->dof_count;
    space_jac_map[chain_id].resize(6,nj);
    space_jac_map[chain_id].setZero();

    base::Vector3d point_position;
    point_position.setZero();
    J.setZero(6, nj);
    int body_id = rbdl_model->GetBodyId(tip_frame.c_str());
    CalcPointJacobian6D(*rbdl_model,q,body_id,point_position,J);

    space_jac_map[chain_id].block(0,0,3,nj) = J.block(3,0,3,nj);
    space_jac_map[chain_id].block(3,0,3,nj) = J.block(0,0,3,nj);

    return space_jac_map[chain_id];
}

const base::MatrixXd &RobotModelRBDL::bodyJacobian(const std::string &root_frame, const std::string &tip_frame){
    if(joint_state.time.isNull()){
        LOG_ERROR("You have to call update() with appropriately timestamped joint data at least once before requesting kinematic information!");
        throw std::runtime_error(" Invalid call to bodyJacobian()");
    }
    if(!hasLink(tip_frame)){
        LOG_ERROR_S << "Request jacobian for " << root_frame << " -> " << tip_frame << " but link " << tip_frame << " does not exist in robot model" << std::endl;
        throw std::runtime_error("Invalid call to bodyJacobian()");
    }
    if(root_frame != world_frame){
        LOG_ERROR_S<<"Requested Forward kinematics computation for kinematic chain "<<root_frame<<"->"<<tip_frame<<" but hyrodyn robot model always requires the root frame to be the root of the full model"<<std::endl;
        throw std::runtime_error("Invalid call to bodyJacobian()");
    }

    std::string chain_id = chainID(root_frame,tip_frame);

    uint nj = rbdl_model->dof_count;
    body_jac_map[chain_id].resize(6,nj);
    body_jac_map[chain_id].setZero();

    J.setZero(6, nj);
    int body_id = rbdl_model->GetBodyId(tip_frame.c_str());
    CalcBodySpatialJacobian(*rbdl_model,q,body_id,J);

    body_jac_map[chain_id].block(0,0,3,nj) = J.block(3,0,3,nj);
    body_jac_map[chain_id].block(3,0,3,nj) = J.block(0,0,3,nj);

    return body_jac_map[chain_id];
}

const base::MatrixXd &RobotModelRBDL::comJacobian(){
    if(joint_state.time.isNull()){
        LOG_ERROR("You have to call update() with appropriately timestamped joint data at least once before requesting kinematic information!");
        throw std::runtime_error(" Invalid call to spatialAccelerationBias()");
    }
    com_jac.setZero(3, rbdl_model->dof_count);
    double total_mass = 0.0;

    // iterate over the moving bodies except base link (numbered 0 in the graph)
    for (unsigned int i = 1; i < rbdl_model->mBodies.size(); i++){
        const Body& body = rbdl_model->mBodies.at(i);
        Math::MatrixNd com_jac_body_i;
        com_jac_body_i.setZero(3, rbdl_model->dof_count);
        CalcPointJacobian(*rbdl_model, q, i, body.mCenterOfMass, com_jac_body_i, true);

        com_jac = com_jac + body.mMass * com_jac_body_i;
        total_mass = total_mass + body.mMass;
    }

    com_jac = (1/total_mass) * com_jac;
    return com_jac;
}

const base::MatrixXd &RobotModelRBDL::jacobianDot(const std::string &root_frame, const std::string &tip_frame){
    throw std::runtime_error("RobotModelRBDL::jacobianDot: Not implemented!");
}

const base::Acceleration &RobotModelRBDL::spatialAccelerationBias(const std::string &root_frame, const std::string &tip_frame){
    if(joint_state.time.isNull()){
        LOG_ERROR("You have to call update() with appropriately timestamped joint data at least once before requesting kinematic information!");
        throw std::runtime_error(" Invalid call to spatialAccelerationBias()");
    }
    if(!hasLink(tip_frame)){
        LOG_ERROR_S << "Request jacobian for " << root_frame << " -> " << tip_frame << " but link " << tip_frame << " does not exist in robot model" << std::endl;
        throw std::runtime_error("Invalid call to spatialAccelerationBias()");
    }
    if(root_frame != world_frame){
        LOG_ERROR_S<<"Requested Forward kinematics computation for kinematic chain "<<root_frame<<"->"<<tip_frame<<" but hyrodyn robot model always requires the root frame to be the root of the full model"<<std::endl;
        throw std::runtime_error("Invalid call to spatialAccelerationBias()");
    }
    base::Vector3d point_position;
    point_position.setZero();
    unsigned int body_id = rbdl_model->GetBodyId(tip_frame.c_str());
    Math::SpatialVector spatial_acceleration = CalcPointAcceleration6D(*rbdl_model, q, qd, Math::VectorNd::Zero(rbdl_model->dof_count), body_id, point_position);
    spatial_acc_bias = base::Acceleration(spatial_acceleration.segment(3,3), spatial_acceleration.segment(0,3));
    return spatial_acc_bias;
}

const base::MatrixXd &RobotModelRBDL::jointSpaceInertiaMatrix(){
    if(joint_state.time.isNull()){
        LOG_ERROR("You have to call update() with appropriately timestamped joint data at least once before requesting kinematic information!");
        throw std::runtime_error(" Invalid call to jointSpaceInertiaMatrix()");
    }
    H_q.setZero(rbdl_model->dof_count, rbdl_model->dof_count);
    CompositeRigidBodyAlgorithm(*rbdl_model, q, H_q);
    joint_space_inertia_mat = H_q;
    return joint_space_inertia_mat;
}

const base::VectorXd &RobotModelRBDL::biasForces(){
    if(joint_state.time.isNull()){
        LOG_ERROR("You have to call update() with appropriately timestamped joint data at least once before requesting kinematic information!");
        throw std::runtime_error(" Invalid call to biasForces()");
    }
    tau.resize(rbdl_model->dof_count);
    InverseDynamics(*rbdl_model, q, qd, Math::VectorNd::Zero(rbdl_model->dof_count), tau);
    bias_forces = tau;
    return bias_forces;
}

const base::samples::RigidBodyStateSE3& RobotModelRBDL::centerOfMass(){
    if(joint_state.time.isNull()){
        LOG_ERROR("You have to call update() with appropriately timestamped joint data at least once before requesting kinematic information!");
        throw std::runtime_error(" Invalid call to centerOfMass()");
    }

    double mass;
    Math::Vector3d com_pos, com_vel, com_acc;
    Utils::CalcCenterOfMass(*rbdl_model, q, qd, &qdd, mass, com_pos, &com_vel, &com_acc);

    com_rbs.frame_id = world_frame;
    com_rbs.pose.position = com_pos;
    com_rbs.pose.orientation.setIdentity();
    com_rbs.twist.linear = com_vel;
    com_rbs.twist.angular.setZero();
    com_rbs.acceleration.linear = com_acc; // TODO: double check CoM acceleration
    com_rbs.acceleration.angular.setZero();
    com_rbs.time = joint_state.time;
    return com_rbs;
}

void RobotModelRBDL::computeInverseDynamics(base::commands::Joints &solver_output){
    if(joint_state.time.isNull()){
        LOG_ERROR("You have to call update() with appropriately timestamped joint data at least once before requesting kinematic information!");
        throw std::runtime_error(" Invalid call to computeInverseDynamics()");
    }
    InverseDynamics(*rbdl_model, q, qd, qdd, tau);
    uint start_idx = 0;
    if(has_floating_base)
        start_idx = 6;
    for(uint i = 0; i < noOfActuatedJoints(); i++){
        solver_output[actuated_joint_names[i]].effort = tau[i+start_idx];
    }
}

}
