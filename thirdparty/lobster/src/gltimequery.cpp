// Copyright 2014 Wouter van Oortmerssen. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "lobster/stdafx.h"

#include "lobster/vmdata.h"
#include "lobster/glinterface.h"
#include "lobster/glincludes.h"

// NOTE: Time queries require OpenGL 3.3 or otherwise GL_ARB_timer_query
// In case this causes problems, make sure to check platform availability

TimeQuery::~TimeQuery() {
    #ifdef PLATFORM_WINNIX
        if (active) {
            GL_CALL(glDeleteQueries(2, query_buffer_ids[0]));
            GL_CALL(glDeleteQueries(2, query_buffer_ids[1]));
            active = !active;
        }
    #endif
}

TimeQuery CreateTimeQuery() {
    return TimeQuery();
}

void TimeQuery::Start() {
    #ifdef PLATFORM_WINNIX
        if (!active) {
            GL_CALL(glGenQueries(2, query_buffer_ids[0]));
            GL_CALL(glGenQueries(2, query_buffer_ids[1]));
            // Otherwise opengl reports errors?
            GL_CALL(glQueryCounter(query_buffer_ids[front_buffer_index][0], GL_TIMESTAMP));
            GL_CALL(glQueryCounter(query_buffer_ids[front_buffer_index][1], GL_TIMESTAMP));
            active = !active;
        }
        GL_CALL(glQueryCounter(query_buffer_ids[back_buffer_index][0], GL_TIMESTAMP));
    #endif
}

void TimeQuery::Stop() {
    #ifdef PLATFORM_WINNIX
        if (!active) return;
        GL_CALL(glQueryCounter(query_buffer_ids[back_buffer_index][1], GL_TIMESTAMP));

        // Retrieve timings
        GLuint64 start = 0;
        GLuint64 end = 0;
        GL_CALL(glGetQueryObjectui64v(query_buffer_ids[front_buffer_index][0], GL_QUERY_RESULT, &start));
        GL_CALL(glGetQueryObjectui64v(query_buffer_ids[front_buffer_index][1], GL_QUERY_RESULT, &end));

        // Flip buffer indices
        front_buffer_index = 1 - front_buffer_index;
        back_buffer_index = 1 - back_buffer_index;

        // Convert into ms
        float timing = (end - start) / 1000000.0f;
        timing_average_buffer[timing_average_buffer_sample] = timing;
        // Collect & average samples
        if (timing_average_buffer_sample + 1u == TIME_QUERY_SAMPLE_COUNT) {
            float sum = 0.0;
            for (uint32_t i = 0u; i < TIME_QUERY_SAMPLE_COUNT; ++i) sum += timing_average_buffer[i];
            timing_average_result = sum / float(TIME_QUERY_SAMPLE_COUNT);
        }
        timing_average_buffer_sample = (timing_average_buffer_sample + 1u) % TIME_QUERY_SAMPLE_COUNT;
        //timing_average_result = timing; // No averaging
    #endif
}

float TimeQuery::GetResult() {
    return timing_average_result;
}
