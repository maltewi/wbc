#ifndef WBC_PY_ROBOT_MODEL_PINOCCHIO_HPP
#define WBC_PY_ROBOT_MODEL_PINOCCHIO_HPP

#include "robot_models/pinocchio/RobotModelPinocchio.hpp"
#include "../../wbc_types_conversions.h"

namespace wbc_py {

/** Wrapper for wbc::RobotModelPinocchio. Unfortunately we have to do this, since base::samples::Joints and other Rock types cannot be easily exposed to python*/
class RobotModelPinocchio : public wbc::RobotModelPinocchio{
public:
    bool configure(const wbc_py::RobotModelConfig &cfg);
    void update(const base::NamedVector<base::JointState> &joint_state);
    void update2(const base::NamedVector<base::JointState> &joint_state, const base::samples::RigidBodyStateSE3 &floating_base_state);
    base::NamedVector<base::JointState> jointState2(const std::vector<std::string> &names);
    void setActiveContacts(const base::NamedVector<wbc::ActiveContact> & active_contacts);
    base::NamedVector<wbc::ActiveContact> getActiveContacts2();
    base::NamedVector<base::JointLimitRange> jointLimits2();
    wbc_py::RobotModelConfig getRobotModelConfig();
};

}

#endif

