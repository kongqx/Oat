//******************************************************************************
//* File:   RecordControl.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//*
//* Copyright (c) Jon Newman (jpnewman snail mit dot edu)
//* All right reserved.
//* This file is part of the Oat project.
//* This is free software: you can redistribute it and/or modify
//* it under the terms of the GNU General Public License as published by
//* the Free Software Foundation, either version 3 of the License, or
//* (at your option) any later version.
//* This software is distributed in the hope that it will be useful,
//* but WITHOUT ANY WARRANTY; without even the implied warranty of
//* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//* GNU General Public License for more details.
//* You should have received a copy of the GNU General Public License
//* along with this source code.  If not, see <http://www.gnu.org/licenses/>.
//******************************************************************************

#include <unordered_map>

#include "RecordControl.h"

namespace oat {

int controlRecorder(std::istream &in,
                    std::ostream &out,
                    oat::Recorder &recorder,
                    bool pretty_cmd) {

    // Command map
    std::unordered_map<std::string, char> cmd_map;
    cmd_map["exit"] = 'e';
    cmd_map["help"] = 'h';
    cmd_map["start"] = 's';
    cmd_map["stop"] = 'S';
    cmd_map["new"] = 'n';
    cmd_map["rename"] = 'r';

    // User control loop
    std::string cmd;
    bool quit = false;

    while (!quit) {
        
        if (pretty_cmd)
            out << ">>> " << std::flush;

        std::getline(in, cmd);

        switch (cmd_map[cmd]) {
            case 'e' :
            {
                quit = true;
                out << "Received exit signal.";
                out << std::endl;
                break;
            }
            case 'h' :
            {
                printInteractiveUsage(out);
                out << std::endl;
                break;
            }
            case 's' :
            {
                recorder.set_record_on(true);
                out << "Recording ON." << std::endl;
                out.flush();
                break;
            }
            case 'S' :
            {
                recorder.set_record_on(false);
                out << "Recording OFF." << std::endl;
                break;
            }
//            case 'n' :
//            {
//                if (recorder.record_on())
//                    std::cerr << "Recording must be paused to create new file.\n";
//                else
//                    //recorder.createFile();
//                break;
//            }
//            case 'r' :
//            {
//                std::cerr << "\'" << cmd << "\' is not implemented.\n";
//                break;
//            }
            default :
            {
                in.clear();
                in.ignore(std::numeric_limits<std::streamsize>::max());
                out << "Invalid command \'" << cmd << "\'" << std::endl;
                break;
            }
        }
    }

    return 0;
}

void printInteractiveUsage(std::ostream &out) {

    out << "COMMANDS\n"
        << "CMD         FUNCTION\n"
        << " help       Print this information.\n"
        << " start      Start recording. This will append and file if it\n"
        << "            already exists.\n"
        << " stop       Pause recording. This will pause\n"
        << "            recording and will not start a new file.\n"
        //<< " new        Start new file. Start time will be used to create\n"
        //<< "            unique file name.\n"
        //<< " rename     Specify a new file location. User will be prompted\n"
        //<< "            to select a new save location.\n"
        << " exit       Exit the program.\n";
}

void printRemoteUsage(std::ostream &out) {

    out << "Recorder is under remote control.\n"
        << "Commands provided through STDIN have no effect.\n";
}

} /* namespace oat */

