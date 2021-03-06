/*
 *  This file is part of Permafrost Engine. 
 *  Copyright (C) 2018 Eduard Permyakov 
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

/*****************************************************************************/
/* INPUTS                                                                    */
/*****************************************************************************/

in VertexToFrag {
         vec2 uv;
    flat int  mat_idx;
         vec3 world_pos;
         vec3 normal;
}from_vertex;

/*****************************************************************************/
/* OUTPUTS                                                                   */
/*****************************************************************************/

out vec4 o_frag_color;

/*****************************************************************************/
/* UNIFORMS                                                                  */
/*****************************************************************************/

uniform vec3 ambient_color;
uniform vec3 light_color;
uniform vec3 light_pos;
uniform vec3 view_pos;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform sampler2D texture3;
uniform sampler2D texture4;
uniform sampler2D texture5;
uniform sampler2D texture6;
uniform sampler2D texture7;
uniform sampler2D texture8;
uniform sampler2D texture9;
uniform sampler2D texture10;
uniform sampler2D texture11;
uniform sampler2D texture12;
uniform sampler2D texture13;
uniform sampler2D texture14;
uniform sampler2D texture15;

/*****************************************************************************/
/* PROGRAM                                                                   */
/*****************************************************************************/

void main()
{
    vec4 tex_color;

    switch(from_vertex.mat_idx) {
    case 0:  tex_color = texture(texture0,  from_vertex.uv); break;
    case 1:  tex_color = texture(texture1,  from_vertex.uv); break;
    case 2:  tex_color = texture(texture2,  from_vertex.uv); break;
    case 3:  tex_color = texture(texture3,  from_vertex.uv); break;
    case 4:  tex_color = texture(texture4,  from_vertex.uv); break;
    case 5:  tex_color = texture(texture5,  from_vertex.uv); break;
    case 6:  tex_color = texture(texture6,  from_vertex.uv); break;
    case 7:  tex_color = texture(texture7,  from_vertex.uv); break;
    case 8:  tex_color = texture(texture8,  from_vertex.uv); break;
    case 9:  tex_color = texture(texture9,  from_vertex.uv); break;
    case 10: tex_color = texture(texture10, from_vertex.uv); break;
    case 11: tex_color = texture(texture11, from_vertex.uv); break;
    case 12: tex_color = texture(texture12, from_vertex.uv); break;
    case 13: tex_color = texture(texture13, from_vertex.uv); break;
    case 14: tex_color = texture(texture14, from_vertex.uv); break;
    case 15: tex_color = texture(texture15, from_vertex.uv); break;
    }

    /* Simple alpha test to reject transparent pixels */
    if(tex_color.a== 0.0)
        discard;

    o_frag_color = vec4(tex_color.xyz, 1.0);
}

