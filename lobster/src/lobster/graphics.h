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

#include "lobster/vmdata.h"

#include "lobster/glinterface.h"

extern lobster::ResourceType mesh_type;
extern lobster::ResourceType texture_type;
extern lobster::ResourceType shader_type;

extern lobster::Value UpdateBufferObjectResource(lobster::VM &vm, lobster::Value buf, const void *data,
                                                 size_t len, ptrdiff_t offset, bool ssbo, bool dyn);
extern void BindBufferObjectResource(lobster::VM &vm, lobster::Value buf, string_view_nt name);

extern Texture GetTexture(const lobster::Value &res);
