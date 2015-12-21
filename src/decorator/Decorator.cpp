//******************************************************************************
//* File:   Decorator.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//
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
//****************************************************************************

#include <exception>
#include <string>
#include <tuple>
#include <vector>
#include <opencv2/opencv.hpp>
#include <ctime>
#include <cmath>

#include "../../lib/datatypes/Position2D.h"

#include "Decorator.h"

namespace oat {

Decorator::Decorator(const std::vector<std::string> &position_source_addresses,
                     const std::string &frame_source_address,
                     const std::string &frame_sink_address) :
  name_("decorator[" + frame_source_address+ "->" + frame_sink_address + "]")
, frame_source_address_(frame_source_address)
, frame_sink_address_(frame_sink_address)
{

    if (!position_source_addresses.empty()) {
        for (auto &addr : position_source_addresses) {

            oat::Position2D pos(addr);

            auto t = std::make_tuple(std::move(addr),
                                     std::move(pos),
                                     new oat::Source<oat::Position2D>());

            position_sources_.push_back(t);
        }
    } else {
        decorate_position_ = false;
    }
}

Decorator::~Decorator() {

    // Delete the memory pointed to by `new oat::Source<oat::Position2D>()`
    for (auto &pos : position_sources_)
        delete std::get<2>(pos);
}

void Decorator::connectToNodes() {

    // Establish our a slot in the node
    frame_source_.touch(frame_source_address_);

    // Wait for synchronous start with sink when it binds the node
    frame_source_.connect();

    // Get frame meta data to format sink
    oat::Source<oat::SharedFrameHeader>::ConnectionParameters param =
            frame_source_.parameters();

    // Connect to position source nodes
    for (auto &pos : position_sources_)
        std::get<2>(pos)->touch(std::get<0>(pos));

    // Verify connections to position source nodes
    for (auto &pos : position_sources_)
        std::get<2>(pos)->connect();

    // Bind to sink sink node and create a shared cv::Mat
    frame_sink_.bind(frame_sink_address_, param.bytes);
    shared_frame_ = frame_sink_.retrieve(param.rows, param.cols, param.type);
}

bool Decorator::decorateFrame() {

    // 1. Get frame
    // START CRITICAL SECTION //
    ////////////////////////////

    // Wait for sink to write to node
    if (frame_source_.wait() == oat::NodeState::END )
        return true;

    // Clone the shared frame
    frame_source_.copyTo(internal_frame_);

    // Tell sink it can continue
    frame_source_.post();

    ////////////////////////////
    //  END CRITICAL SECTION  //

    // 2. Get positions
    for (auto &pos : position_sources_) {

        // START CRITICAL SECTION //
        ////////////////////////////
        if (std::get<2>(pos)->wait() == oat::NodeState::END )
            return true;

        std::get<1>(pos) = std::get<2>(pos)->clone();

        std::get<2>(pos)->post();
        ////////////////////////////
        //  END CRITICAL SECTION  //
    }

    // Decorate frame
    drawSymbols();

    // START CRITICAL SECTION //
    ////////////////////////////

    // Wait for sources to read
    frame_sink_.wait();

    internal_frame_.copyTo(shared_frame_);

    // Tell sources there is new data
    frame_sink_.post();

    ////////////////////////////
    //  END CRITICAL SECTION  //

    // None of the sink's were at the END state
    return false;
}

void Decorator::drawSymbols() {

    if (decorate_position_) {
        drawPosition();
        drawHeading();
        drawVelocity();

        if (print_region_)
            printRegion();
    }

    if (print_timestamp_)
        printTimeStamp();

    if (print_sample_number_)
        printSampleNumber();

    if (encode_sample_number_)
        encodeSampleNumber();
}

void Decorator::drawPosition() {

    size_t i = 0;
    for (auto pos : position_sources_) {
        if (std::get<1>(pos).position_valid) {

            cv::circle(internal_frame_,
                       std::get<1>(pos).position,
                       position_circle_radius_,
                       pos_colors_[i],
                       line_thickness_);
        }

        (i > position_sources_.size() - 1) ? i = 0 : i++;
    }
}

void Decorator::drawHeading() {

    for (auto pos : position_sources_) {
        if (std::get<1>(pos).position_valid && std::get<1>(pos).heading_valid) {

            cv::Point2d start = std::get<1>(pos).position - (heading_line_length_ * std::get<1>(pos).heading);
            cv::Point2d end = std::get<1>(pos).position + (heading_line_length_ * std::get<1>(pos).heading);

            // Draw heading line
            cv::line(internal_frame_, start, end, font_color_, 1, 8);

//            double angle = std::atan2((double) start.y - end.y, (double) start.x - end.x);
//            start.x = end.x + heading_arrow_length_ * std::cos(angle + PI / 4);
//            start.y = end.y + heading_arrow_length_ * std::sin(angle + PI / 4);
//            cv::line(internal_frame_, start, end, font_color_, 1, 8);
//            start.x = end.x + heading_arrow_length_ * std::cos(angle - PI / 4);
//            start.y = end.y + heading_arrow_length_ * std::sin(angle - PI / 4);
//            cv::line(internal_frame_, start, end, font_color_, 1, 8);
        }
    }
}

void Decorator::drawVelocity() {

     size_t i = 0;
    for (auto pos : position_sources_) {

        if (std::get<1>(pos).velocity_valid && std::get<1>(pos).position_valid) {
            cv::Point2d end = std::get<1>(pos).position + (velocity_scale_factor_ * std::get<1>(pos).velocity);
            cv::line(internal_frame_,
                     std::get<1>(pos).position,
                     end,
                     pos_colors_[i],
                     line_thickness_);
        }

        (i > position_sources_.size() - 1) ? i = 0 : i++;
    }
}

void Decorator::printRegion() {

    // Create display string
    std::string reg_text ;

    if (position_sources_.size() == 1)
        reg_text = "Region:";
    else
        reg_text = "Regions:";

    // Calculate text origin based upon message size
    int baseline = 0;
    cv::Size reg_text_size =
            cv::getTextSize(reg_text, font_type_, font_scale_, font_thickness_, &baseline);

    cv::Point text_origin(10, reg_text_size.height);
    cv::putText(internal_frame_, reg_text, text_origin, font_thickness_, font_scale_, font_color_);

    // Add ID: region information
    size_t i = 0;
    for (auto pos : position_sources_) {
        if (std::get<1>(pos).region_valid)
            reg_text = std::get<0>(pos) + ": " + std::string(std::get<1>(pos).region);
        else
            reg_text = std::get<0>(pos) + ": ?";

        text_origin.y += reg_text_size.height + 2;
        cv::putText(internal_frame_,
                    reg_text, text_origin,
                    font_thickness_,
                    font_scale_,
                    pos_colors_[i]);

        (i > position_sources_.size() - 1) ? i = 0 : i++;
    }
}

void Decorator::printTimeStamp() {

    std::time_t raw_time;
    struct tm * time_info;
    char buffer[80];

    std::time(&raw_time);
    time_info = std::localtime(&raw_time);

    std::strftime(buffer, 80, "%c", time_info);

    cv::Point text_origin(internal_frame_.cols - 230, internal_frame_.rows - 10);
    cv::putText(internal_frame_, std::string(buffer), text_origin, 1, font_scale_, font_color_);
}

void Decorator::printSampleNumber() {

    cv::Point text_origin(10, internal_frame_.rows - 10);
    cv::putText(internal_frame_,
                std::to_string(internal_frame_.sample_count()),
                text_origin,
                1,
                font_scale_,
                font_color_);
}

/**
 * Encode the current sample number into the first row of the matrix
 */
void Decorator::encodeSampleNumber() {

    uint64_t sample_count = internal_frame_.sample_count();

    int column = internal_frame_.cols - 64 * encode_bit_size_;

    if (column < 0)
        throw std::runtime_error("Binary counter bar is too large for frame.");

    for (int shift = 0; shift < 64; shift++) {

        cv::Mat sub_square = internal_frame_.colRange(column, column + encode_bit_size_).rowRange(0, encode_bit_size_);

        if (sample_count & 0x1) {

            cv::Mat true_mat(encode_bit_size_, encode_bit_size_, internal_frame_.type(), CV_RGB(255, 255, 255));
            true_mat.copyTo(sub_square);

        } else {

            cv::Mat false_mat = cv::Mat::zeros(encode_bit_size_, encode_bit_size_, internal_frame_.type());
            false_mat.copyTo(sub_square);
        }

        sample_count >>= 1;
        column += encode_bit_size_;
    }
}

} /* namespace oat */
