/*
 *  This file is part of Permafrost Engine. 
 *  Copyright (C) 2017-2018 Eduard Permyakov 
 *
 *  Permafrost Engine is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Permafrost Engine is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#version 330 core

layout (location = 0) in vec3  in_pos;
layout (location = 1) in vec2  in_uv;
layout (location = 2) in vec3  in_normal;
layout (location = 3) in int   in_material_idx;
layout (location = 4) in int   in_blend_mode;
layout (location = 5) in ivec4 in_adjacent_mat_indices;

/*****************************************************************************/
/* OUTPUTS                                                                   */
/*****************************************************************************/

out VertexToFrag {
         vec2  uv;
    flat int   mat_idx;
         vec3  world_pos;
         vec3  normal;
    flat int   blend_mode;
    flat ivec4 adjacent_mat_indices;
}to_fragment;

out VertexToGeo {
    vec3 normal;
}to_geometry;

/*****************************************************************************/
/* UNIFORMS                                                                  */
/*****************************************************************************/

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

/*****************************************************************************/
/* PROGRAM
/*****************************************************************************/

void main()
{
    to_fragment.uv = in_uv;
    to_fragment.mat_idx = in_material_idx;
    to_fragment.world_pos = (model * vec4(in_pos, 1.0)).xyz;
    to_fragment.normal = normalize(mat3(model) * in_normal);
    to_fragment.blend_mode = in_blend_mode;
    to_fragment.adjacent_mat_indices = in_adjacent_mat_indices;

    to_geometry.normal = normalize(mat3(projection * view * model) * in_normal);

    gl_Position = projection * view * model * vec4(in_pos, 1.0);
}

