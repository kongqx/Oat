//******************************************************************************
//* Copyright (c) Jon Newman (jpnewman at mit snail edu) 
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
//******************************************************************************

#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>

#include <sys/stat.h>
#include <boost/filesystem.hpp>

#include "Recorder.h"

namespace bfs = boost::filesystem;

Recorder::Recorder(const std::vector<std::string>& position_source_names,
        const std::vector<std::string>& frame_source_names,
        std::string save_path,
        std::string file_name,
        const bool& append_date,
        const int& frames_per_second) :
  save_path(save_path)
, file_name(file_name)
, append_date(append_date)
, running(true)
, frames_per_second(frames_per_second)
, frame_client_idx(0)
, position_client_idx(0)
, frame_read_success(false)
, position_labels(position_source_names) {

    // First check that the save_path is valid
    bfs::path path(save_path.c_str());
    if (!bfs::exists(path) || !bfs::is_directory(path)) {
        std::cout << "Warning: requested recording path, " + save_path + ", "
                << "does not exist, or is not a valid directory.\n"
                << "attempting to use the current directory instead.\n";
        save_path = bfs::current_path().c_str();
    }

    std::time_t raw_time;
    struct tm * time_info;
    char buffer[100];

    std::time(&raw_time);
    time_info = std::localtime(&raw_time);
    std::strftime(buffer, 80, "%F-%H-%M-%S", time_info);
    std::string date_now = std::string(buffer);

    // Setup position sources
    if (!position_source_names.empty()) {

        for (auto &name : position_source_names) {

            position_sources.push_back(new oat::SMClient<oat::Position2D>(name));
            source_positions.push_back(new oat::Position2D);
        }

        // Create a single position file
        std::string posi_fid;
        if (append_date)
            posi_fid = file_name.empty() ?
            (save_path + "/" + date_now + "_" + position_source_names[0]) :
            (save_path + "/" + date_now + "_" + file_name);
        else
            posi_fid = file_name.empty() ?
            (save_path + "/" + position_source_names[0]) :
            (save_path + "/" + file_name);

        posi_fid = posi_fid + ".json";

        checkFile(posi_fid);

        position_fp = fopen(posi_fid.c_str(), "wb");
        if (!position_fp) {
            std::cerr << "Error: unable to open, " + posi_fid + ". Exiting." << std::endl;
            exit(EXIT_FAILURE);
        }

        file_stream = new rapidjson::FileWriteStream(position_fp, position_write_buffer, sizeof (position_write_buffer));
        json_writer.Reset(*file_stream);
        json_writer.StartArray();
    }

    // Create a video writer, file, and buffer for each image stream
    uint32_t idx = 0;
    if (!frame_source_names.empty()) {

        for (auto &frame_source_name : frame_source_names) {

            // Generate file name for this video
            std::string frame_fid;
            if (append_date)
                frame_fid = file_name.empty() ?
                (save_path + "/" + date_now + "_" + frame_source_name) :
                (save_path + "/" + date_now + "_" + file_name + "_" + frame_source_name);
            else
                frame_fid = file_name.empty() ?
                (save_path + "/" + frame_source_name) :
                (save_path + "/" + file_name + "_" + frame_source_name);

            frame_fid = frame_fid + ".avi";

            checkFile(frame_fid);

            video_file_names.push_back(frame_fid);
            frame_sources.push_back(new oat::MatClient(frame_source_name));
            frame_write_buffers.push_back(new
                    boost::lockfree::spsc_queue
                    < cv::Mat, boost::lockfree::capacity < frame_write_buffer_size> >);
            video_writers.push_back(new cv::VideoWriter());

            // TODO: provide threads with function
            frame_write_mutexes.push_back(new std::mutex());
            frame_write_condition_variables.push_back(new std::condition_variable());
            frame_write_threads.push_back(new std::thread(&Recorder::writeFramesToFileFromBuffer, this, idx++));

        }
    } else {
        frame_read_success = true;
    }

}

Recorder::~Recorder() {

    // Set running to false to trigger thread join
    running = false;

    // Join all threads
    for (auto &value : frame_write_threads) {
        value->join();
        delete value;
    }

    // Free all dynamically allocated resources
    for (auto &value : frame_sources) {
        delete value;
    }

    for (auto &value : video_writers) {
        value->release();
        delete value;
    }

    for (auto &value : frame_write_threads) {
        delete &value;
    }

    for (auto &value : frame_write_mutexes) {
        delete &value;
    }

    for (auto &value : frame_write_condition_variables) {
        delete &value;
    }

    for (auto &value : frame_write_buffers) {
        delete value;
    }

    for (auto &value : position_sources) {
        delete value;
    }

    if (position_fp) {
        json_writer.EndArray();
        file_stream->Flush();
        delete file_stream;
    }
}

void Recorder::writeStreams() {

    while (frame_client_idx < frame_sources.size()) {

        if (!(frame_read_success = (frame_sources[frame_client_idx]->getSharedMat(current_frame)))) {
            break;

        } else {

            // Push newest frame into client N's queue
            frame_write_buffers[frame_client_idx]->push(current_frame);

            // Notify a writer thread that there is new data in the queue
            frame_write_condition_variables[frame_client_idx]->notify_one();

            frame_client_idx++;
        }
    }

    // Get current positions
    while (position_client_idx < position_sources.size()) {

        if (!(position_sources[position_client_idx]->getSharedObject(*source_positions[position_client_idx]))) {
            return;
        }

        position_client_idx++;
    }

    if (!frame_read_success) {
        return;
    }

    // Reset the position client read counter
    frame_client_idx = 0;
    position_client_idx = 0;

    // Write the frames to file
    //writeFramesToBuffer(); TOD: callback on write to buffer.
    writePositionsToFile();

}

void Recorder::writeFramesToFileFromBuffer(uint32_t writer_idx) {

    while (running) {

        std::unique_lock<std::mutex> lk(*frame_write_mutexes[writer_idx]);
        frame_write_condition_variables[writer_idx]->wait_for(lk, std::chrono::milliseconds(10));

        if (frame_write_buffers[writer_idx]->read_available()) {
            if (!video_writers[writer_idx]->isOpened()) {
                initializeWriter(*video_writers[writer_idx],
                        video_file_names.at(writer_idx),
                        frame_write_buffers[writer_idx]->front());
            }


            video_writers[writer_idx]->write(frame_write_buffers[writer_idx]->front());
            frame_write_buffers[writer_idx]->pop();
        }
    }
}

void Recorder::writePositionsToFile() {

    if (position_fp) {
        json_writer.StartArray();

        //json_writer.String("sample");
        json_writer.Uint(position_sources[0]->get_current_time_stamp());

        //json_writer.String("positions");
        json_writer.StartArray();

        int idx = 0;
        for (auto pos : source_positions) {

            pos->Serialize(json_writer, position_labels[idx]);
            ++idx;
        }

        json_writer.EndArray();
        json_writer.EndArray();
    }
}

void Recorder::initializeWriter(cv::VideoWriter& writer,
        const std::string& file_name,
        const cv::Mat& image) {

    // Initialize writer using the first frame taken from server
    int fourcc = CV_FOURCC('H', '2', '6', '4');
    writer.open(file_name, fourcc, frames_per_second, image.size());

}

bool Recorder::checkFile(std::string& file) {

    int i = 0;
    std::string original_file = file;
    bool file_exists = false;

    while (bfs::exists(file.c_str())) {

        ++i;
        bfs::path path(original_file.c_str());
        bfs::path parent_path = path.parent_path();
        bfs::path stem = path.stem();
        bfs::path extension = path.extension();

        std::string append = "_" + std::to_string(i);
        stem += append.c_str();

        // Recreate file name
        file = std::string(parent_path.generic_string()) +
                "/" +
                std::string(stem.generic_string()) +
                std::string(extension.generic_string());

    }

    if (i != 0) {
        std::cout << "Warning: " + original_file + " exists.\n"
                << "File renamed to: " + file + ".\n";
        file_exists = true;
    }

    return file_exists;
}