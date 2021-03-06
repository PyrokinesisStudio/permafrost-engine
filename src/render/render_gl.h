/*
 *  This file is part of Permafrost Engine. 
 *  Copyright (C) 2017 Eduard Permyakov 
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

#ifndef RENDER_GL_H
#define RENDER_GL_H

#include <GL/glew.h>

#include <stddef.h>
#include <stdbool.h>


struct render_private;
struct vertex;
struct tile;

void R_GL_Init(struct render_private *priv, const char *shader, const struct vertex *vbuff);
void R_GL_TileGetVertices(const struct tile *tile, struct vertex *out, size_t r, size_t c);

/* ---------------------------------------------------------------------------
 * Patch the vertices for a particular tile to have adjacency information
 * about the neighboring tiles, to be used for smooth blending.
 * Tiles which border tiles with different materials will get blending
 * 'turned on' by setting a vertex attribute.
 * ---------------------------------------------------------------------------
 */
void R_GL_TilePatchVertsBlend(GLuint VBO, const struct tile *tiles, 
                              int width, int height, int r, int c);

#endif
