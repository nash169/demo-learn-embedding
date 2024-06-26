#!/usr/bin/env python
# encoding: utf-8
#
#    This file is part of kernel-lib.
#
#    Copyright (c) 2020, 2021, 2022 Bernardo Fichera <bernardo.fichera@gmail.com>
#
#    Permission is hereby granted, free of charge, to any person obtaining a copy
#    of this software and associated documentation files (the "Software"), to deal
#    in the Software without restriction, including without limitation the rights
#    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#    copies of the Software, and to permit persons to whom the Software is
#    furnished to do so, subject to the following conditions:
#
#    The above copyright notice and this permission notice shall be included in all
#    copies or substantial portions of the Software.
#
#    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#    SOFTWARE.

import os
from wafbuild.utils import load

VERSION = "1.0.0"
APPNAME = "franka-control"

libname = "FrankaControl"
srcdir = "src"
blddir = "build"
libdir = "franka_control"

compiler = "cxx"
optional = [
    "utilslib",  # for managing reading/writing files
    "controllib",  # provides different types of low levels controllers
    "beautifulbullet",  # robotic simulator
    "zmqstream",  # stream across Python/C++
    "optitracklib",  # communicate with the optitrack system
    "frankacontrol",  # control franka robot
    "yamlcpp"
]

required_bld = {
    "src/sim_os.cpp": ["UTILSLIB", "BEAUTIFULBULLET", "CONTROLLIB", "ZMQSTREAM", "YAMLCPP"],
    "src/sim_ik.cpp": ["UTILSLIB", "BEAUTIFULBULLET", "CONTROLLIB", "ZMQSTREAM", "YAMLCPP"],
    "src/sim_id.cpp": ["UTILSLIB", "BEAUTIFULBULLET", "CONTROLLIB", "ZMQSTREAM", "YAMLCPP"],
    "src/exp_os.cpp": ["UTILSLIB", "FRANKACONTROL", "CONTROLLIB", "ZMQSTREAM", "YAMLCPP"],
    "src/exp_ik.cpp": ["UTILSLIB", "FRANKACONTROL", "CONTROLLIB", "ZMQSTREAM", "YAMLCPP"],
    "src/exp_id.cpp": ["UTILSLIB", "FRANKACONTROL", "CONTROLLIB", "ZMQSTREAM", "YAMLCPP"],
}


def options(opt):
    # Add build shared library options
    opt.add_option("--shared",
                   action="store_true",
                   help="build shared library")

    # Add build static library options
    opt.add_option("--static",
                   action="store_true",
                   help="build static library")

    # Load library options
    load(opt, compiler, required=None, optional=optional)


def configure(cfg):
    # Load library configurations
    load(cfg, compiler, required=None, optional=optional)


def build(bld):
    sources = []
    for root, _, filenames in os.walk('src'):
        sources += [os.path.join(root, filename) for filename in filenames if filename.endswith(('.cpp', '.cc'))]

    for example in sources:
        if example in required_bld:
            if set(required_bld[example]).issubset(bld.env["libs"]):
                bld.program(
                    features="cxx",
                    source=example,
                    uselib=bld.env["libs"],
                    target=example[:-len(".cpp")],
                )
        else:
            bld.program(
                features="cxx",
                source=example,
                uselib=bld.env["libs"],
                target=example[:-len(".cpp")],
            )
