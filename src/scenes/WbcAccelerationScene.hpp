#ifndef WBCACCELERATIONSCENE_HPP
#define WBCACCELERATIONSCENE_HPP

#include "../core/WbcScene.hpp"
#include "../core/JointAccelerationConstraint.hpp"
#include "../core/CartesianAccelerationConstraint.hpp"

namespace wbc{

typedef std::shared_ptr<CartesianAccelerationConstraint> CartesianAccelerationConstraintPtr;
typedef std::shared_ptr<JointAccelerationConstraint> JointAccelerationConstraintPtr;

class WbcAccelerationScene : public WbcScene{
protected:
    base::VectorXd q_dot;
    base::VectorXd solver_output_acc, robot_acc, robot_vel;

    /**
     * brief Create a constraint and add it to the WBC scene
     */
    virtual ConstraintPtr createConstraint(const ConstraintConfig &config);



public:
    WbcAccelerationScene(RobotModelPtr robot_model) :
        WbcScene(robot_model){}
    virtual ~WbcAccelerationScene(){
    }
    /**
     * @brief Update the wbc scene and return the (updated) optimization problem
     * @param ctrl_output Control solution that fulfill the given constraints as good as possible
     */
    virtual void update();

    /**
     * @brief evaluateConstraints Evaluate the fulfillment of the constraints given the current robot state and the solver output
     */
    virtual const ConstraintsStatus &updateConstraintsStatus(const base::samples::Joints& solver_output, const base::samples::Joints& joint_state);
};

} // namespace wbc

#endif
