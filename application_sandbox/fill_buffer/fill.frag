/* Copyright 2017 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#version 450

layout(location = 0) out vec4 out_color;
layout (location = 2) in vec3 normal;

layout (binding = 2, set = 0) uniform filled_data {
    vec4 multiplier;
};

void main() {
    out_color = vec4(normal * multiplier.xyz, 1.0);
}