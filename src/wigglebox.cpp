/*
 * Copyright (C) 2015 Emmanuel Durand
 *
 * This file is part of WiggleBox.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * blobserver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WiggleBox.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "wigglebox.h"

#include <spawn.h>

using namespace std;

/*************/
void WiggleBox::parseArguments(int argc, char** argv)
{
    for (int i = 1; i < argc;)
    {
        if ("--help" == string(argv[i]) || "-h" == string(argv[i]))
        {
            cout << "Wigglebox engine - Where the fun is side-shifted!" << endl;
            cout << "Basic usage: wigglebox" << endl;
            exit(0);
        }

        ++i;
    }
}

/*************/
WiggleBox::WiggleBox(int argc, char** argv)
{
    parseArguments(argc, argv);

    _httpServer = unique_ptr<HttpServer>(new HttpServer("127.0.0.1", "9090"));
    _httpServerThread = thread([&]() {
        _httpServer->run();
    });

    _camera = unique_ptr<RgbdCamera>(new RgbdCamera());
    _wiggler = unique_ptr<Wiggler>(new Wiggler());
}

/*************/
WiggleBox::~WiggleBox()
{
}

/*************/
void WiggleBox::run()
{
    vector<cv::Mat> wiggledFrames;
    int wiggleIndex = 0;

    while(_state.run)
    {
        auto frameBegin = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

        if (_camera->isReady())
        {
            _camera->grab();

            auto depthMap = _camera->retrieveDisparity();
            auto rgbFrame = _camera->retrieveRGB();

            if (rgbFrame.rows > 0 && rgbFrame.cols > 0 && depthMap.cols > 0 && depthMap.rows > 0)
            {
                if (!_state.grabWiggle)
                {
                    cv::imshow("image", rgbFrame);
                    cv::imshow("depth", depthMap);

                    if (wiggledFrames.size() != 0)
                    {
                        cv::imshow("wiggle", wiggledFrames[wiggleIndex]);
                        wiggleIndex = (wiggleIndex + 1) % wiggledFrames.size();
                    }
                }
                else
                {
                    _wiggler->setInputs(rgbFrame, depthMap);            

                    wiggledFrames.clear();
                    float wiggleFactor = -_state.wiggleRange;
                    float wiggleStep = _state.wiggleStep;
                    int steps = (_state.wiggleRange / _state.wiggleStep) * 4 + 1;

                    for (auto i = 1; i < steps; ++i)
                    {
                        if (wiggleFactor >= _state.wiggleRange)
                            wiggleStep = -1.f * _state.wiggleStep;
                        else if (wiggleFactor <= -_state.wiggleRange)
                            wiggleStep = _state.wiggleStep;
                        wiggleFactor += wiggleStep;

                        _wiggler->setWiggleFactor(wiggleFactor);
                        _wiggler->setWiggleDist(48.f * 32.f);

                        auto wiggledFrame = _wiggler->doWiggle();
                        wiggledFrames.push_back(cv::Mat(wiggledFrame, cv::Rect(0, 32, 640, 480 - 32)));
                    }

                    convertToGif(wiggledFrames);

                    _state.grabWiggle = false;
                }
            }
        }

        // Handle HTTP requests
        auto requestHandler = _httpServer->getRequestHandler();
        pair<RequestHandler::Command, RequestHandler::ReturnFunction> message;
        while ((message = requestHandler->getNextCommand()).first.command != RequestHandler::CommandId::nop)
        {
            auto command = message.first;
            auto replyFunction = message.second;

            if (command.command == RequestHandler::CommandId::quit)
            {
                _state.run = false;
                message.second(true, {"Default reply"});
            }
            else if (command.command == RequestHandler::CommandId::capture)
            {
                _state.grabWiggle = true;
                message.second(true, {"Capturing"});
            }
            else if (command.command == RequestHandler::CommandId::getCaptureName)
            {
                message.second(true, {_basename + ".gif"});
            }
        }

        // Handle keyboard
        auto frameEnd = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        long frameDuration = 33; // 33ms per frame, so 30fps
        auto sleepTime = std::max(1l, frameDuration - ((long)frameEnd - (long)frameBegin));

        short key = cv::waitKey(sleepTime);
        processKeyEvent(key);
    }

    return;
}

/*************/
void WiggleBox::convertToGif(const vector<cv::Mat>& frames)
{
    string basename = _basename;
    int index = 0;
    for (auto& frame : frames)
    {
        cv::imwrite("/tmp/" + basename + to_string(index) + ".jpg", frame);
        index++;
    }

    string cmd = "convertToGif";
    char* argv[] = {(char*)"convertToGif", (char*)basename.c_str(), nullptr};

    int pid;
    posix_spawn(&pid, cmd.c_str(), nullptr, nullptr, argv, nullptr);
}

/*************/
void WiggleBox::processKeyEvent(short key)
{
    switch (key)
    {
    default:
        //cout << "Pressed key: " << key << endl;
        break;
    case 27: // Escape
        _state.run = false;
        break;
    case 32: // Space
        _state.grabWiggle = true;
        break;
    }
}

/*************/
int main(int argc, char** argv)
{
    WiggleBox wigglebox(argc, argv);
    wigglebox.run();
    return 0;
}
