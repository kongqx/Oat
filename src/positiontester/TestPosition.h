//******************************************************************************
//* File:   TestPosition.h
//* Author: Jon Newman <jpnewman snail mit dot edu>
//*
//* Copyright (c) Jon Newman (jpnewman snail mit dot edu) 
//* All right reserved.
//* This file is part of the Simple Tracker project.
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
//*****************************************************************************

#ifndef TESTPOSITION_H
#define	TESTPOSITION_H

#include <chrono>
#include <string>
#include <random>
#include <opencv2/core/mat.hpp>

#include "../../lib/datatypes/Position.h"
#include "../../lib/shmem/BufferedSMServer.h"

/**
 * Abstract test position server.
 * All concrete test position server types implement this ABC.
 */
template <class T>
class TestPosition  {
    
public:

    /**
     * Abstract test position server.
     * All concrete test position server types implement this ABC and can be used
     * to server test positions with different motion characteristics to test
     * subsequent processing steps.
     * @param position_sink_name Position SINK to publish test positions
     * @param samples_per_second Sample rate in Hz
     */
    TestPosition(const std::string& position_sink_name, const double samples_per_second = 30) : 
      name("testpos[*->" + position_sink_name + "]")
    , position_sink(position_sink_name)
    , sample(0) { 

        generateSamplePeriod(samples_per_second);
        tick = clock.now();
    }
      
    virtual ~TestPosition() { }

    /**
     * Generate test position. Publish test position to SINK.
     * @return End-of-stream signal. If true, this component should exit.
     */
    bool process(void) {
        
        // Publish simulated position
        position_sink.pushObject(generatePosition(), sample);
        ++sample;
        
        return false;   
    }
    
    /**
     * Configure test position server parameters.
     * @param config_file configuration file path
     * @param config_key configuration key
     */
    virtual void configure(const std::string& file_name, const std::string& key) = 0;

    /**
     * Get test position server name.
     * @return name 
     */
    std::string get_name(void) const {return name; }

protected:
    
    /**
     * Generate test position.
     * @return Test position.
     */
    virtual T generatePosition(void) = 0;
    
    // Test position sample clock
    std::chrono::high_resolution_clock clock;
    std::chrono::duration<double> sample_period_in_sec;
    std::chrono::high_resolution_clock::time_point tick;
    
    /**
     * Configure the sample period
     * @param samples_per_second Sample period in seconds.
     */
    void generateSamplePeriod(const double samples_per_second) {

        std::chrono::duration<double> period{1.0 / samples_per_second};

        // Automatic conversion
        sample_period_in_sec = period;
    }    
        
private:
    
    // Test position name
    std::string name;

    // The test position SINK
    oat::BufferedSMServer<T> position_sink;
    
    // Test position sample number
    uint32_t sample;
};

// Explicit declaration
template class TestPosition<oat::Position2D>;

#endif	/* TESTPOSITION_H */

