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

#include "movement.h"
#include "public/game.h"
#include "../config.h"
#include "../camera.h"
#include "../asset_load.h"
#include "../event.h"
#include "../entity.h"
#include "../script/public/script.h"
#include "../render/public/render.h"
#include "../map/public/map.h"
#include "../lib/public/kvec.h"
#include "../lib/public/khash.h"
#include "../anim/public/anim.h"

#include <assert.h>
#include <SDL.h>


/* For the purpose of movement simulation, all entities have the same mass,
 * meaning they are accelerate the same amount when applied equal forces. */
#define ENTITY_MASS (1.0f)
#define EPSILON     (1.0f/1024)
#define MAX_FORCE   (1.0f)

enum arrival_state{
    /* Entity is moving towards the flock's destination point */
    STATE_MOVING,
    /* Entity is in proximity of the flock's destination point, 
     * it is looking for a good point to stop. */
    STATE_SETTLING,
    /* Entity is considered to have arrived and no longer moving. */
    STATE_ARRIVED
};

struct movestate{
    vec2_t             velocity;
    enum arrival_state state;
};

KHASH_MAP_INIT_INT(entity, struct entity *)
KHASH_MAP_INIT_INT(state, struct movestate)

struct flock{
    khash_t(entity) *ents;
    vec2_t           target_xz;
};

/* Parameters controlling steering/flocking behaviours */
#define MOVE_SEPARATION_FORCE_SCALE     (1.6f)
#define MOVE_ARRIVE_FORCE_SCALE         (0.7f)
#define MOVE_COHESION_FORCE_SCALE       (0.1f)
#define MOVE_ALIGN_FORCE_SCALE          (0.1f)
#define SETTLE_SEPARATION_FORCE_SCALE   (3.2f)

#define ARRIVE_THRESHOLD_DIST           (5.0f)
#define MOVE_SEPARATION_BUFFER_DIST     (8.0f)
#define SETTLE_SEPARATION_BUFFER_DIST   (14.0f)
#define COHESION_NEIGHBOUR_RADIUS       (25.0f)
#define ALIGN_NEIGHBOUR_RADIUS          (10.0f)
#define ARRIVE_SLOWING_RADIUS           (10.0f)
#define ADJACENCY_SEP_DIST              (10.0f)

#define SETTLE_STOP_TOLERANCE           (0.05f)

/*****************************************************************************/
/* STATIC VARIABLES                                                          */
/*****************************************************************************/

kvec_t(struct entity*)  s_move_markers;
kvec_t(struct flock)    s_flocks;
khash_t(state)         *s_entity_state_table;
const struct map       *s_map;

/*****************************************************************************/
/* STATIC FUNCTIONS                                                          */
/*****************************************************************************/

static bool entities_equal(struct entity **a, struct entity **b)
{
    return (0 == memcmp(*a, *b, sizeof(struct entity)));
}

static void vec2_truncate(vec2_t *inout, float max_len)
{
    if(PFM_Vec2_Len(inout) > max_len) {

        PFM_Vec2_Normal(inout, inout);
        PFM_Vec2_Scale(inout, max_len, inout);
    }
}

static void on_marker_anim_finish(void *user, void *event)
{
    int idx;
    struct entity *ent = user;
    assert(ent);

    kv_indexof(struct entity*, s_move_markers, ent, entities_equal, idx);
    assert(idx != -1);
    kv_del(struct entity*, s_move_markers, idx);

    E_Entity_Unregister(EVENT_ANIM_FINISHED, ent->uid, on_marker_anim_finish);
    AL_EntityFree(ent);
}

static bool make_flock_from_selection(const pentity_kvec_t *sel, vec2_t target_xz)
{
    /* First remove the entities in the selection from any active flocks */
    for(int i = 0; i < kv_size(*sel); i++) {

        const struct entity *curr_ent = kv_A(*sel, i);
        if(curr_ent->flags & ENTITY_FLAG_STATIC || curr_ent->max_speed == 0.0f)
            continue;
        /* Remove any flocks which may have become empty. Iterate vector in backwards order 
         * so that we can delete while iterating, since the last element in the vector takes
         * the place of the deleted one. */
        for(int j = kv_size(s_flocks)-1; j >= 0; j--) {

            khiter_t k;
            struct flock *curr_flock = &kv_A(s_flocks, j);
            if((k = kh_get(entity, curr_flock->ents, curr_ent->uid)) != kh_end(curr_flock->ents))
                kh_del(entity, curr_flock->ents, k);

            if(kh_size(curr_flock->ents) == 0) {
                kh_destroy(entity, curr_flock->ents);
                kv_del(struct flock, s_flocks, j);
            }
        }
    }
    
    struct flock new_flock = (struct flock) {
        .ents = kh_init(entity),
        .target_xz = target_xz,
    };

    if(!new_flock.ents)
        return false;

    for(int i = 0; i < kv_size(*sel); i++) {

        int ret;
        const struct entity *curr_ent = kv_A(*sel, i);

        if(curr_ent->flags & ENTITY_FLAG_STATIC || curr_ent->max_speed == 0.0f)
            continue;

        khiter_t k = kh_put(entity, new_flock.ents, curr_ent->uid, &ret);
        assert(ret != -1 && ret != 0);
        kh_value(new_flock.ents, k) = (struct entity*)curr_ent;

        /* When entities are moved from one flock to another, they keep their existing velocity. 
         * Otherwise, entities start out with a velocity of 0. */
        if((k = kh_get(state, s_entity_state_table, curr_ent->uid)) == kh_end(s_entity_state_table)) {

            k = kh_put(state, s_entity_state_table, curr_ent->uid, &ret);
            assert(ret != -1 && ret != 0);
            kh_value(s_entity_state_table, k) = (struct movestate){ 
                .velocity = {0.0f}, 
                .state = STATE_MOVING
            };
            E_Entity_Notify(EVENT_MOTION_START, curr_ent->uid, NULL, ES_ENGINE);

        }else {

            if(kh_value(s_entity_state_table, k).state == STATE_ARRIVED)
                E_Entity_Notify(EVENT_MOTION_START, curr_ent->uid, NULL, ES_ENGINE);
            kh_value(s_entity_state_table, k).state = STATE_MOVING;
        }
    }

    kv_push(struct flock, s_flocks, new_flock);
    return true;
}

size_t adjacent_flock_members(const struct entity *ent, const struct flock *flock, 
                              struct entity *out[])
{
    vec2_t ent_xz_pos = (vec2_t){ent->pos.x, ent->pos.z};
    size_t ret = 0;

    uint32_t key;
    struct entity *curr;
    kh_foreach(flock->ents, key, curr, {

        if(curr == ent)
            continue;

        vec2_t curr_xz_pos = (vec2_t){curr->pos.x, curr->pos.z};
        vec2_t diff;
        PFM_Vec2_Sub(&ent_xz_pos, &curr_xz_pos, &diff);

        if(PFM_Vec2_Len(&diff) <= ent->selection_radius + curr->selection_radius + ADJACENCY_SEP_DIST)
            out[ret++] = curr;  
    });
    return ret;
}

static void move_marker_add(vec3_t pos)
{
    extern const char *g_basepath;
    char path[256];
    strcpy(path, g_basepath);
    strcat(path, "assets/models/arrow");

    struct entity *ent = AL_EntityFromPFObj(path, "arrow-green.pfobj", "__move_marker__");
    assert(ent);

    ent->pos = pos;
    ent->scale = (vec3_t){2.0f, 2.0f, 2.0f};
    E_Entity_Register(EVENT_ANIM_FINISHED, ent->uid, on_marker_anim_finish, ent);

    A_InitCtx(ent, "Converge", 48);
    A_SetActiveClip(ent, "Converge", ANIM_MODE_ONCE, 48);

    kv_push(struct entity*, s_move_markers, ent);
}

static void on_mousedown(void *user, void *event)
{
    SDL_MouseButtonEvent *mouse_event = &(((SDL_Event*)event)->button);

    if(mouse_event->button != SDL_BUTTON_RIGHT)
        return;

    if(G_MouseOverMinimap())
        return;

    if(S_UI_MouseOverWindow(mouse_event->x, mouse_event->y))
        return;

    vec3_t mouse_coord;
    if(!M_Raycast_IntersecCoordinate(&mouse_coord))
        return;

    const pentity_kvec_t *sel = G_Sel_Get();
    if(kv_size(*sel) > 0) {

        move_marker_add(mouse_coord);
        make_flock_from_selection(sel, (vec2_t){mouse_coord.x, mouse_coord.z});
    }
}

static void on_render_3d(void *user, void *event)
{
    for(int i = 0; i < kv_size(s_move_markers); i++) {

        const struct entity *curr = kv_A(s_move_markers, i);
        if(curr->flags & ENTITY_FLAG_ANIMATED)
            A_Update(curr);

        mat4x4_t model;
        Entity_ModelMatrix(curr, &model);
        R_GL_Draw(curr->render_private, &model);
    }
}

static quat_t dir_quat_from_velocity(vec2_t velocity)
{
    assert(PFM_Vec2_Len(&velocity) > EPSILON);

    float angle_rad = atan2(velocity.raw[1], velocity.raw[0]) - M_PI/2.0f;
    return (quat_t) {
        0.0f, 
        1.0f * sin(angle_rad / 2.0f),
        0.0f,
        cos(angle_rad / 2.0f)
    };
}

/* Seek behaviour makes the entity target and approach a particular destination point.
 */
static vec2_t seek_force(const struct entity *ent, const struct flock *flock, int tick_res)
{
    vec2_t ret, desired_velocity;
    vec2_t pos_xz = (vec2_t){ent->pos.x, ent->pos.z};

    PFM_Vec2_Sub((vec2_t*)&flock->target_xz, &pos_xz, &desired_velocity);
    PFM_Vec2_Normal(&desired_velocity, &desired_velocity);
    PFM_Vec2_Scale(&desired_velocity, ent->max_speed / tick_res, &desired_velocity);

    khiter_t k = kh_get(state, s_entity_state_table, ent->uid);
    assert(k != kh_end(s_entity_state_table));
    vec2_t velocity = kh_value(s_entity_state_table, k).velocity;

    PFM_Vec2_Sub(&desired_velocity, &velocity, &ret);
    return ret;
}

/* Arrival behaviour is like 'seek' but the entity decelerates and comes to a halt when it is 
 * within a threshold radius of the destination point.
 */
static vec2_t arrive_force(const struct entity *ent, const struct flock *flock, int tick_res)
{
    vec2_t ret, desired_velocity;
    vec2_t pos_xz = (vec2_t){ent->pos.x, ent->pos.z};
    float distance;

    PFM_Vec2_Sub((vec2_t*)&flock->target_xz, &pos_xz, &desired_velocity);
    distance = PFM_Vec2_Len(&desired_velocity);

    PFM_Vec2_Normal(&desired_velocity, &desired_velocity);
    PFM_Vec2_Scale(&desired_velocity, ent->max_speed / tick_res, &desired_velocity);

    if(distance < ARRIVE_SLOWING_RADIUS) {
        PFM_Vec2_Scale(&desired_velocity, distance / ARRIVE_SLOWING_RADIUS, &desired_velocity);
    }

    khiter_t k = kh_get(state, s_entity_state_table, ent->uid);
    assert(k != kh_end(s_entity_state_table));
    vec2_t velocity = kh_value(s_entity_state_table, k).velocity;

    PFM_Vec2_Sub(&desired_velocity, &velocity, &ret);
    vec2_truncate(&ret, MAX_FORCE);
    return ret;
}

/* Alignment is a behaviour that causes a particular agent to line up with agents close by.
 */
static vec2_t alignment_force(const struct entity *ent, const struct flock *flock, int tick_res)
{
    vec2_t ret = (vec2_t){0.0f};
    size_t neighbour_count = 0;

    uint32_t key;
    struct entity *curr;
    kh_foreach(flock->ents, key, curr, {

        if(curr == ent)
            continue;

        vec2_t diff;
        vec2_t ent_xz_pos = (vec2_t){ent->pos.x, ent->pos.z};
        vec2_t curr_xz_pos = (vec2_t){curr->pos.x, curr->pos.z};

        PFM_Vec2_Sub(&curr_xz_pos, &ent_xz_pos, &diff);
        if(PFM_Vec2_Len(&diff) < ALIGN_NEIGHBOUR_RADIUS) {

            khiter_t k = kh_get(state, s_entity_state_table, curr->uid);
            assert(kh_exist(s_entity_state_table, k));
            vec2_t velocity = kh_value(s_entity_state_table, k).velocity;

            if(PFM_Vec2_Len(&velocity) < EPSILON)
                continue; 

            PFM_Vec2_Add(&ret, &velocity, &ret);
            neighbour_count++;
        }
    });

    if(0 == neighbour_count)
        return (vec2_t){0.0f};

    khiter_t k = kh_get(state, s_entity_state_table, ent->uid);
    assert(kh_exist(s_entity_state_table, k));
    vec2_t velocity = kh_value(s_entity_state_table, k).velocity;

    PFM_Vec2_Scale(&ret, 1.0f / neighbour_count, &ret);
    PFM_Vec2_Sub(&ret, &velocity, &ret);
    vec2_truncate(&ret, MAX_FORCE);
    return ret;
}

/* Cohesion is a behaviour that causes agents to steer towards the center of mass of nearby agents.
 */
static vec2_t cohesion_force(const struct entity *ent, const struct flock *flock, int tick_res)
{
    vec2_t COM = (vec2_t){0.0f};
    size_t neighbour_count = 0;

    uint32_t key;
    struct entity *curr;
    kh_foreach(flock->ents, key, curr, {

        if(curr == ent)
            continue;

        vec2_t diff;
        vec2_t ent_xz_pos = (vec2_t){ent->pos.x, ent->pos.z};
        vec2_t curr_xz_pos = (vec2_t){curr->pos.x, curr->pos.z};

        PFM_Vec2_Sub(&curr_xz_pos, &ent_xz_pos, &diff);
        if(PFM_Vec2_Len(&diff) < COHESION_NEIGHBOUR_RADIUS) {

            vec2_t xz_pos = (vec2_t){curr->pos.x, curr->pos.z};
            PFM_Vec2_Add(&COM, &xz_pos, &COM);
            neighbour_count++;
        }
    });

    if(0 == neighbour_count)
        return (vec2_t){0.0f};

    vec2_t xz_pos = (vec2_t){ent->pos.x, ent->pos.z};
    PFM_Vec2_Scale(&COM, 1.0f / neighbour_count, &COM);

    vec2_t ret;
    PFM_Vec2_Sub(&COM, &xz_pos, &ret);
    vec2_truncate(&ret, MAX_FORCE);
    return ret;
}

/* Separation is a behaviour that causes agents to steer away from nearby agents.
 */
static vec2_t separation_force(const struct entity *ent, const struct flock *flock, int tick_res,
                               float buffer_dist)
{
    const float NEIGHBOUR_RADIUS = ent->selection_radius + buffer_dist;

    vec2_t ret = (vec2_t){0.0f};
    size_t neighbour_count = 0;

    uint32_t key;
    struct entity *curr;
    kh_foreach(flock->ents, key, curr, {

        if(curr == ent)
            continue;

        vec2_t diff;
        vec2_t ent_xz_pos = (vec2_t){ent->pos.x, ent->pos.z};
        vec2_t curr_xz_pos = (vec2_t){curr->pos.x, curr->pos.z};

        PFM_Vec2_Sub(&curr_xz_pos, &ent_xz_pos, &diff);
        if(PFM_Vec2_Len(&diff) < NEIGHBOUR_RADIUS) {

            float frac = 1.0f - (PFM_Vec2_Len(&diff) / NEIGHBOUR_RADIUS);
            PFM_Vec2_Scale(&diff, frac, &diff);
            PFM_Vec2_Add(&ret, &diff, &ret);
            neighbour_count++;
        }
    });

    if(0 == neighbour_count)
        return (vec2_t){0.0f};

    PFM_Vec2_Scale(&ret, -1.0f / neighbour_count, &ret);
    vec2_truncate(&ret, MAX_FORCE);
    return ret;
}

static vec2_t total_steering_force(const struct entity *ent, const struct flock *flock, int tick_res)
{
    khiter_t k = kh_get(state, s_entity_state_table, ent->uid);
    assert(kh_exist(s_entity_state_table, k));
    enum arrival_state state = kh_value(s_entity_state_table, k).state;

    vec2_t arrive = arrive_force(ent, flock, tick_res);
    vec2_t cohesion = cohesion_force(ent, flock, tick_res);
    vec2_t alignment = alignment_force(ent, flock, tick_res);

    vec2_t ret = (vec2_t){0.0f};
    switch(state) {
    case STATE_MOVING: {
        vec2_t separation = separation_force(ent, flock, tick_res, MOVE_SEPARATION_BUFFER_DIST);

        PFM_Vec2_Scale(&separation, MOVE_SEPARATION_FORCE_SCALE, &separation);
        PFM_Vec2_Scale(&arrive,     MOVE_ARRIVE_FORCE_SCALE,     &arrive);
        PFM_Vec2_Scale(&cohesion,   MOVE_COHESION_FORCE_SCALE,   &cohesion);
        PFM_Vec2_Scale(&alignment,  MOVE_ALIGN_FORCE_SCALE,      &alignment);

        PFM_Vec2_Add(&ret, &separation, &ret);
        PFM_Vec2_Add(&ret, &arrive, &ret);
        PFM_Vec2_Add(&ret, &cohesion, &ret);
        PFM_Vec2_Add(&ret, &alignment, &ret);

        break;
    }
    case STATE_SETTLING: {
        vec2_t separation = separation_force(ent, flock, tick_res, SETTLE_SEPARATION_BUFFER_DIST);

        PFM_Vec2_Scale(&separation, SETTLE_SEPARATION_FORCE_SCALE, &separation);
        PFM_Vec2_Add(&ret, &separation, &ret);

        break;
    }
    case STATE_ARRIVED:
        break;
    default: assert(0);
    }

    vec2_truncate(&ret, MAX_FORCE);
    return ret;
}

static void on_30hz_tick(void *user, void *event)
{
    const int TICK_RES = 30;

    /* Iterate vector backwards so we can delete entries while iterating. */
    for(int i = kv_size(s_flocks)-1; i >= 0; i--) {

        uint32_t key;
        struct entity *curr;

        /******************************************************************
         * First, decide if we can disband this flock
         *****************************************************************/
        bool disband = true;
        kh_foreach(kv_A(s_flocks, i).ents, key, curr, {

            khiter_t k = kh_get(state, s_entity_state_table, curr->uid);
            assert(kh_exist(s_entity_state_table, k));
            if(kh_value(s_entity_state_table, k).state != STATE_ARRIVED) {
                disband = false;
                break;
            }
        });

        if(disband) {
            kh_destroy(entity, kv_A(s_flocks, i).ents);
            kv_del(struct flock, s_flocks, i);
            continue;
        }

        kh_foreach(kv_A(s_flocks, i).ents, key, curr, {
        
            /******************************************************************
             * Compute acceleration 
             *****************************************************************/
            vec2_t steer_accel, new_velocity; 
            vec2_t steer_force = total_steering_force(curr, &kv_A(s_flocks, i), TICK_RES);
            PFM_Vec2_Scale(&steer_force, 1.0f / ENTITY_MASS, &steer_accel);

            /******************************************************************
             * Compute new velocity
             *****************************************************************/
            khiter_t k = kh_get(state, s_entity_state_table, curr->uid);
            assert(kh_exist(s_entity_state_table, k));
            vec2_t old_velocity = kh_value(s_entity_state_table, k).velocity; 

            PFM_Vec2_Add(&old_velocity, &steer_accel, &new_velocity);
            vec2_truncate(&new_velocity, curr->max_speed / TICK_RES);

            /******************************************************************
             * Update position and rotation
             *****************************************************************/
            vec2_t xz_pos = (vec2_t){curr->pos.x, curr->pos.z};
            vec2_t new_xz_pos;
            PFM_Vec2_Add(&xz_pos, &new_velocity, &new_xz_pos);
            new_xz_pos = M_ClampedMapCoordinate(s_map, new_xz_pos);
            curr->pos = (vec3_t){new_xz_pos.raw[0], M_HeightAtPoint(s_map, new_xz_pos), new_xz_pos.raw[1]};

            if(PFM_Vec2_Len(&new_velocity) > EPSILON) {
                curr->rotation = dir_quat_from_velocity(new_velocity);
            }

            /******************************************************************
             * Update state of entity
             *****************************************************************/
            kh_value(s_entity_state_table, k).velocity = new_velocity;

            switch(kh_value(s_entity_state_table, k).state) {
            case STATE_MOVING: {

                vec2_t diff_to_target;
                vec2_t xz_pos = (vec2_t){curr->pos.x, curr->pos.z};
                PFM_Vec2_Sub(&kv_A(s_flocks, i).target_xz, &xz_pos, &diff_to_target);
                if(PFM_Vec2_Len(&diff_to_target) < ARRIVE_THRESHOLD_DIST){

                    kh_value(s_entity_state_table, k) = (struct movestate) {
                        .state = STATE_ARRIVED,
                        .velocity = (vec2_t){0.0f}
                    };
                    E_Entity_Notify(EVENT_MOTION_END, curr->uid, NULL, ES_ENGINE);
                }

                struct entity *adjacent[kh_size(kv_A(s_flocks, i).ents)]; 
                size_t num_adj = adjacent_flock_members(curr, &kv_A(s_flocks, i), adjacent);

                for(int j = 0; j < num_adj; j++) {

                    khiter_t l = kh_get(state, s_entity_state_table, adjacent[j]->uid);
                    assert(kh_exist(s_entity_state_table, l));

                    if(kh_value(s_entity_state_table, l).state == STATE_ARRIVED
                    || kh_value(s_entity_state_table, l).state == STATE_SETTLING) {

                        kh_value(s_entity_state_table, k).state = STATE_SETTLING;
                        break;
                    }
                }
                break;
            }
            case STATE_SETTLING: {

                if(PFM_Vec2_Len(&new_velocity) < SETTLE_STOP_TOLERANCE * curr->max_speed)  {

                    kh_value(s_entity_state_table, k) = (struct movestate) {
                        .state = STATE_ARRIVED,
                        .velocity = (vec2_t){0.0f}
                    };
                    E_Entity_Notify(EVENT_MOTION_END, curr->uid, NULL, ES_ENGINE);
                }
                break;
            }
            case STATE_ARRIVED: 
                break;
            default: 
                assert(0);
            }

        });
    }
}

/*****************************************************************************/
/* EXTERN FUNCTIONS                                                          */
/*****************************************************************************/

bool G_Move_Init(const struct map *map)
{
    assert(map);
    if(NULL == (s_entity_state_table = kh_init(state)))
        return false;
    kv_init(s_move_markers);
    kv_init(s_flocks);
    E_Global_Register(SDL_MOUSEBUTTONDOWN, on_mousedown, NULL);
    E_Global_Register(EVENT_RENDER_3D, on_render_3d, NULL);
    E_Global_Register(EVENT_30HZ_TICK, on_30hz_tick, NULL);

    s_map = map;
    return true;
}

void G_Move_Shutdown(void)
{
    s_map = NULL;

    E_Global_Unregister(EVENT_30HZ_TICK, on_30hz_tick);
    E_Global_Unregister(EVENT_RENDER_3D, on_render_3d);
    E_Global_Unregister(SDL_MOUSEBUTTONDOWN, on_mousedown);

    for(int i = 0; i < kv_size(s_move_markers); i++) {
        E_Entity_Unregister(EVENT_ANIM_FINISHED, kv_A(s_move_markers, i)->uid, on_marker_anim_finish);
        AL_EntityFree(kv_A(s_move_markers, i));
    }
    kv_destroy(s_move_markers);
    kv_destroy(s_flocks);
    kh_destroy(state, s_entity_state_table);
}

