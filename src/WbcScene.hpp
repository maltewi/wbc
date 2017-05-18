#ifndef WBCSCENE_HPP
#define WBCSCENE_HPP

#include "ConstraintConfig.hpp"
#include "Constraint.hpp"

namespace wbc{

class RobotModel;
class OptProblem;

class WbcScene{
protected:
    std::vector< std::vector<Constraint*> > constraints;
    std::vector< ConstraintsPerPrio > constraint_vector;
    RobotModel* model;

    /** Create a constraint and add it to the WBC scene*/
    virtual Constraint* createConstraint(const ConstraintConfig &config) = 0;

    /** Delete all constraints and free memory*/
    void clearConstraints();

public:
    WbcScene(){}
    virtual ~WbcScene(){}

    /** Configure the WBC scene. Create constraints and sort them by priority*/
    void configure(const std::vector<ConstraintConfig> &config);

    /** Update the wbc scene and return the current solver output*/
    virtual void setupOptProblem(RobotModel* model, OptProblem& opt_problem) = 0;

    /** Return a Particular constraint. Throw if the constraint does not exist */
    Constraint* getConstraint(const std::string& name);

    /** True in case the given constraint exists */
    bool hasConstraint(const std::string& name);

    /** Returns all constraints as vector */
    std::vector< ConstraintsPerPrio > getConstraints();

    /** Returns the number of constraint variables per priority */
    static std::vector<int> getConstraintVariablesPerPrio(const std::vector<ConstraintConfig> &config);

    /** Sort constraint config by the priorities of the constraints */
    static void sortConstraintConfig(const std::vector<ConstraintConfig>& config, std::vector< std::vector<ConstraintConfig> >& sorted_config);
};

} // namespace wbc

#endif
