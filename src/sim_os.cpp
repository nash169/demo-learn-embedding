/*
    This file is part of beautiful-bullet.

    Copyright (c) 2021, 2022 Bernardo Fichera <bernardo.fichera@gmail.com>

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

// Simulator
#include <beautiful_bullet/Simulator.hpp>

// Graphics
#include <beautiful_bullet/graphics/MagnumGraphics.hpp>

// Spaces
#include <control_lib/spatial/R.hpp>
#include <control_lib/spatial/SE.hpp>
#include <control_lib/spatial/SO.hpp>

// Controllers
#include <control_lib/controllers/Feedback.hpp>

// CPP Utils
#include <utils_lib/FileManager.hpp>
#include <utils_lib/Timer.hpp>

// Stream
#include <zmq_stream/Requester.hpp>

// parse yaml
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <iostream>
#include <thread>

using namespace beautiful_bullet;
using namespace control_lib;
using namespace utils_lib;
using namespace std::chrono;
using namespace zmq_stream;

using R3 = spatial::R<3>;
using SE3 = spatial::SE<3>;
using SO3 = spatial::SO<3, true>;

struct ParamsDS {
    struct controller : public defaults::controller {
        // Integration time step controller
        PARAM_SCALAR(double, dt, 1.0e-2);
    };

    struct feedback : public defaults::feedback {
        // Output dimension
        PARAM_SCALAR(size_t, d, 3);
    };
};

struct ParamsCTR {
    struct controller : public defaults::controller {
        // Integration time step controller
        PARAM_SCALAR(double, dt, 1.0e-2);
    };

    struct feedback : public defaults::feedback {
        // Output dimension
        PARAM_SCALAR(size_t, d, 6);
    };
};

struct FrankaModel : public bodies::MultiBody {
public:
    FrankaModel() : bodies::MultiBody("rsc/franka/panda.urdf"), _frame("panda_joint_8"), _reference(pinocchio::LOCAL_WORLD_ALIGNED) {}

    Eigen::MatrixXd jacobian(const Eigen::VectorXd& q)
    {
        return static_cast<bodies::MultiBody*>(this)->jacobian(q, _frame, _reference);
    }

    Eigen::MatrixXd jacobianDerivative(const Eigen::VectorXd& q, const Eigen::VectorXd& dq)
    {
        return static_cast<bodies::MultiBody*>(this)->jacobianDerivative(q, dq, _frame, _reference);
    }

    Eigen::Matrix<double, 6, 1> framePose(const Eigen::VectorXd& q)
    {
        return static_cast<bodies::MultiBody*>(this)->framePose(q, _frame);
    }

    Eigen::Matrix<double, 6, 1> frameVelocity(const Eigen::VectorXd& q, const Eigen::VectorXd& dq)
    {
        return static_cast<bodies::MultiBody*>(this)->frameVelocity(q, dq, _frame, _reference);
    }

    std::string _frame;
    pinocchio::ReferenceFrame _reference;
};

struct TaskDynamics : public controllers::AbstractController<ParamsDS, SE3> {
    TaskDynamics()
    {
        _d = SE3::dimension();
        _u.setZero(_d);

        // ds
        _pos.setStiffness(5.0 * Eigen::MatrixXd::Identity(3, 3));
        _rot.setStiffness(1.0 * Eigen::MatrixXd::Identity(3, 3));

        // external ds stream
        _external = false;
        _requester.configure("localhost", "5511");
    }

    TaskDynamics& setReference(const SE3& x)
    {
        _pos.setReference(R3(x._trans));
        _rot.setReference(SO3(x._rot));
        return *this;
    }

    const bool& external() { return _external; }

    TaskDynamics& setExternal(const bool& value)
    {
        _external = value;
        return *this;
    }

    void update(const SE3& x) override
    {
        // position ds
        _u.head(3) = _external ? _requester.request<Eigen::VectorXd>(x._trans, 3) : _pos(R3(x._trans));

        // orientation ds
        // _u.tail(3) = _rot(SO3(x._rot));
        _u.tail(3).setZero();
    }

protected:
    using AbstractController<ParamsDS, SE3>::_d;
    using AbstractController<ParamsDS, SE3>::_xr;
    using AbstractController<ParamsDS, SE3>::_u;

    controllers::Feedback<ParamsDS, R3> _pos;
    controllers::Feedback<ParamsDS, SO3> _rot;

    bool _external;
    Requester _requester;
};

struct OperationSpaceController : public control::MultiBodyCtr {
    OperationSpaceController(const std::shared_ptr<FrankaModel>& model, const SE3& ref_pose)
        : control::MultiBodyCtr(ControlMode::CONFIGURATIONSPACE), _ref_pose(ref_pose), _model(model)
    {
        // ds
        _ds.setReference(_ref_pose);

        // damping operation space control
        Eigen::Matrix<double, 6, 6> damping = Eigen::Matrix<double, 6, 6>::Zero();
        damping.diagonal() << 20.0, 20.0, 20.0, 1.0, 1.0, 1.0;
        _ctr.setDamping(damping);

        // writer
        _writer.setFile("demo_os_0.csv");
    }

    Eigen::VectorXd action(bodies::MultiBody& body) override
    {
        // state
        Eigen::VectorXd q = body.state(), dq = body.velocity();
        SE3 curr_pose(_model->framePose(q));

        if (_ds.external())
            _writer.append(curr_pose._trans.transpose());

        if ((curr_pose._trans - _ref_pose._trans).norm() <= 0.05 && !_ds.external())
            _ds.setExternal(true);

        Eigen::Matrix<double, 7, 1> tau;
        {
            Timer timer;
            Eigen::Matrix<double, 6, 7> jac = _model->jacobian(q);
            curr_pose._v = jac * dq;
            _ref_pose._v = _ds(curr_pose);
            _ctr.setReference(_ref_pose);
            tau = jac.transpose() * _ctr(curr_pose);
        }

        return tau;
    }

    // reference
    SE3 _ref_pose;
    // task space ds
    TaskDynamics _ds;
    // ctr
    controllers::Feedback<ParamsCTR, SE3> _ctr;
    // model
    std::shared_ptr<FrankaModel> _model;
    // file manager
    FileManager _writer;
};

int main(int argc, char const* argv[])
{
    // Create simulator
    Simulator simulator;

    // Add graphics
    simulator.setGraphics(std::make_unique<graphics::MagnumGraphics>());

    // Add ground
    simulator.addGround();

    // Multi Bodies
    auto franka = std::make_shared<FrankaModel>();
    Eigen::VectorXd state_ref = (franka->positionUpper() - franka->positionLower()) * 0.5 + franka->positionLower();
    franka->setState(state_ref);

    // trajectory
    std::string demo = (argc > 1) ? "demo_" + std::string(argv[1]) : "demo_1";
    YAML::Node config = YAML::LoadFile("rsc/demos/" + demo + "/dynamics_params.yaml");
    auto offset = config["offset"].as<std::vector<double>>();

    FileManager mng;
    std::vector<Eigen::MatrixXd> trajectories;
    for (size_t i = 1; i <= 7; i++) {
        trajectories.push_back(mng.setFile("rsc/demos/" + demo + "/trajectory_" + std::to_string(i) + ".csv").read<Eigen::MatrixXd>());
        trajectories.back().rowwise() += Eigen::Map<Eigen::Vector3d>(&offset[0]).transpose();
        static_cast<graphics::MagnumGraphics&>(simulator.graphics()).app().trajectory(trajectories.back(), i >= 4 ? "red" : "blue");
    }

    // task space target
    Eigen::Vector3d ref_pos = trajectories[0].row(0);
    Eigen::Matrix3d ref_rot = (Eigen::Matrix3d() << 0.768647, 0.239631, 0.593092, 0.0948479, -0.959627, 0.264802, 0.632602, -0.147286, -0.760343).finished();
    SE3 ref_pose(ref_rot, ref_pos);

    auto controller = std::make_shared<OperationSpaceController>(franka, ref_pose);

    // Set controlled robot
    (*franka)
        .activateGravity()
        .addControllers(controller);

    // Add robots and run simulation
    simulator.add(static_cast<bodies::MultiBodyPtr>(franka));

    // run
    // simulator.run();
    simulator.initGraphics();

    size_t index = 0;
    double t = 0.0, dt = 1e-3, T = 20.0;

    auto next = steady_clock::now();
    auto prev = next - 1ms;

    while (t <= T) {
        auto now = steady_clock::now();

        if (!simulator.step(size_t(t / dt)))
            break;

        t += dt;

        if ((franka->framePosition(franka->state()) - Eigen::Map<Eigen::Vector3d>(&offset[0])).norm() <= 0.05)
            break;

        prev = now;
        next += 1ms;
        std::this_thread::sleep_until(next);
    }

    return 0;
}