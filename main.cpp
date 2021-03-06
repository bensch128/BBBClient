/*****************************************************************************
*                                                                            *
*  OpenNI 2.x Alpha                                                          *
*  Copyright (C) 2012 PrimeSense Ltd.                                        *
*                                                                            *
*  This file is part of OpenNI.                                              *
*                                                                            *
*  Licensed under the Apache License, Version 2.0 (the "License");           *
*  you may not use this file except in compliance with the License.          *
*  You may obtain a copy of the License at                                   *
*                                                                            *
*      http://www.apache.org/licenses/LICENSE-2.0                            *
*                                                                            *
*  Unless required by applicable law or agreed to in writing, software       *
*  distributed under the License is distributed on an "AS IS" BASIS,         *
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
*  See the License for the specific language governing permissions and       *
*  limitations under the License.                                            *
*                                                                            *
*****************************************************************************/
#include <stdio.h>
#include <OpenNI.h>

#include <time.h>

#include <iostream>

#include <boost/thread.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/atomic.hpp>

#include "OniSampleUtilities.h"

#define SAMPLE_READ_WAIT_TIMEOUT 2000 //2000ms

using namespace openni;

int captureFrame()
/**
* This captures the frame and puts it on the lock free queue
*
* @author benjamin(10/20/2014)
*/
{

    Status rc = OpenNI::initialize();
    if (rc != STATUS_OK)
    {
        printf("Initialize failed\n%s\n", OpenNI::getExtendedError());
        return 1;
    }

    Device device;
    rc = device.open(ANY_DEVICE);
    if (rc != STATUS_OK)
    {
        printf("Couldn't open device\n%s\n", OpenNI::getExtendedError());
        return 1;
    }

    VideoStream depth;

    if (device.getSensorInfo(SENSOR_DEPTH)!= NULL)
    {
        rc = depth.create(device, SENSOR_DEPTH);
        if (rc != STATUS_OK)
        {
            printf("Couldn't create depth stream\n%s\n", OpenNI::getExtendedError());
            return 3;
        }
    }

    rc = depth.start();
    if (rc != STATUS_OK)
    {
        printf("Couldn't start the depth stream\n%s\n", OpenNI::getExtendedError());
        return 4;
    }

    VideoFrameRef frame;

    // This is for track the fps
    time_t last = time(NULL);
    uint numFrames = 0;
    while (!wasKeyboardHit())
    {
        int changedStreamDummy;
        VideoStream *pStream = &depth;
        rc = OpenNI::waitForAnyStream(&pStream, 1, &changedStreamDummy, SAMPLE_READ_WAIT_TIMEOUT);
        if (rc != STATUS_OK)
        {
            printf("Wait failed! (timeout is %d ms)\n%s\n", SAMPLE_READ_WAIT_TIMEOUT, OpenNI::getExtendedError());
            continue;
        }

        rc = depth.readFrame(&frame);
        if (rc != STATUS_OK)
        {
            printf("Read failed!\n%s\n", OpenNI::getExtendedError());
            continue;
        }

        if (frame.getVideoMode().getPixelFormat()!= PIXEL_FORMAT_DEPTH_1_MM && frame.getVideoMode().getPixelFormat()!= PIXEL_FORMAT_DEPTH_100_UM)
        {
            printf("Unexpected frame format\n");
            continue;
        }

        DepthPixel *pDepth = (DepthPixel *)frame.getData();

        int middleIndex = (frame.getHeight()+ 1)* frame.getWidth()/ 2;

        printf("[%08llu] %8d\n",(long long)frame.getTimestamp(), pDepth[middleIndex]);

        time_t now = time(NULL);
        if ((now - last)>= 1)
        {
            printf("%d fps", numFrames);
            std::cout << ", now = " << now << ", last = " << last << std::endl;
            last = now;
            numFrames = 0;
        }
        numFrames += 1;
    }

    depth.stop();
    depth.destroy();
    device.close();
    OpenNI::shutdown();

    return 0;
}

int gProducerCount = 0;
boost::atomic_int  gConsumerCount(0);

boost::lockfree::spsc_queue<int, boost::lockfree::capacity<1024> > gSPSCQueue;

const int ITERATIONS = 10000000;

void producer(void)
{
    for (int i = 0; i < ITERATIONS; ++i)
    {
        int value = ++gProducerCount;
        while (!gSPSCQueue.push(value)) { } // do nothing until the value is pushed
    }
}

boost::atomic_bool gDone(false);

void consumer(void)
{
    int value;
    while (!gDone)
    {
        while (gSPSCQueue.read_available() > 0)
        {
            gSPSCQueue.pop(value);
            ++gConsumerCount;
        }

        boost::this_thread::sleep_for(boost::chrono::microseconds(25));
    }

    while (gSPSCQueue.read_available() > 0)
    {
        gSPSCQueue.pop(value);
        ++gConsumerCount;
    }
}

int main()
{
   using namespace std;

   cout << "boost::lockfree::queue is ";
   if (!gSPSCQueue.is_lock_free())
   {
       cout << "not ";
   }
   cout << "lockfree" << endl;

   boost::thread producerThread(producer);
   boost::thread consumerThread(consumer);

   producerThread.join();
   gDone = true;
   consumerThread.join();

   cout << "produced " << gProducerCount << " objects." << endl;
   cout << "consumed " << gConsumerCount << " objects." << endl;

    return 0;
}
