/* J.10.6.1: bounded, exact ray/BSP interval traversal.
 * Uses plane intersections, never point sampling or an epsilon that can
 * erase a nonzero air interval. The engine supplies its native node reader.
 * Header-only so the same production routine can be tested without the engine.
 */
#ifndef WORLD_SOLID_INTERVAL_H
#define WORLD_SOLID_INTERVAL_H
#include <float.h>
#include <math.h>
#include <string.h>

#define WBSP_MAX_DEPTH 256
#define WBSP_MAX_VISITS 8192
#define WBSP_SOLID_CONTENTS (-2)

typedef struct wbsp_node_s
{
    double normal[3], distance;
    int children[2];
} wbsp_node_t;
typedef int (*wbsp_read_node_t)( void *, int, wbsp_node_t * );

typedef struct wbsp_interval_s
{
    double begin, end, clear_end;
    int entry_node, entry_side, exit_node, exit_side;
} wbsp_interval_t;

typedef struct wbsp_frame_s
{
    int node, depth;
    double begin, end;
    int begin_node, begin_side, end_node, end_side;
} wbsp_frame_t;

static int WBSP_Finite( double value )
{
    return value == value && value <= DBL_MAX && value >= -DBL_MAX;
}

/* Return the FIRST solid interval after a verified non-solid ray prefix,
 * followed by its complete contiguous non-solid interval, bounded by length.
 * A start in solid or a solid with no known exit is deliberately unresolved.
 * Boundary sides identify the solid side of the plane for surface normals.
 */
static int WBSP_FirstSolidInterval( void *context, wbsp_read_node_t read_node,
    int root, const double origin[3], const double direction[3], double length,
    wbsp_interval_t *out )
{
    wbsp_frame_t stack[WBSP_MAX_DEPTH];
    wbsp_interval_t interval;
    int count = 1, visits = 0, phase = 0, have_prefix = 0, i;
    double norm = 0;
    if( !read_node || !origin || !direction || !out ||
        !WBSP_Finite( length ) || length <= 0 || root < 0 )
        return 0;
    for( i = 0; i < 3; ++i )
    {
        if( !WBSP_Finite( origin[i] ) || !WBSP_Finite( direction[i] ))
            return 0;
        norm += direction[i] * direction[i];
    }
    if( !WBSP_Finite( norm ) || fabs( norm - 1.0 ) > 0.001 )
        return 0;
    memset( &interval, 0, sizeof( interval ));
    interval.entry_node = interval.exit_node = -1;
    stack[0].node = root;
    stack[0].depth = 0;
    stack[0].begin = 0;
    stack[0].end = length;
    stack[0].begin_node = stack[0].end_node = -1;
    stack[0].begin_side = stack[0].end_side = 0;
    while( count )
    {
        wbsp_frame_t frame = stack[--count];
        while( frame.node >= 0 )
        {
            wbsp_node_t plane;
            double base, slope, first, last, split;
            int side, node = frame.node;
            if( ++visits > WBSP_MAX_VISITS || ++frame.depth > WBSP_MAX_DEPTH ||
                !read_node( context, node, &plane ))
                return 0;
            base = -plane.distance;
            slope = 0;
            norm = 0;
            if( !WBSP_Finite( plane.distance )) return 0;
            for( i = 0; i < 3; ++i )
            {
                if( !WBSP_Finite( plane.normal[i] )) return 0;
                base += origin[i] * plane.normal[i];
                slope += direction[i] * plane.normal[i];
                norm += plane.normal[i] * plane.normal[i];
            }
            if( !WBSP_Finite( base ) || !WBSP_Finite( slope ) ||
                !WBSP_Finite( norm ) || fabs( norm - 1.0 ) > 0.001 )
                return 0;
            first = base + slope * frame.begin;
            last = base + slope * frame.end;
            if( !WBSP_Finite( first ) || !WBSP_Finite( last )) return 0;
            if( first >= 0 && last >= 0 )
            {
                frame.node = plane.children[0];
                continue;
            }
            if( first < 0 && last < 0 )
            {
                frame.node = plane.children[1];
                continue;
            }
            if( slope == 0 ) return 0;
            split = -base / slope;
            if( !WBSP_Finite( split ) || split < frame.begin || split > frame.end )
                return 0;
            side = slope > 0 ? 1 : 0;
            if( split < frame.end )
            {
                wbsp_frame_t far_frame = frame;
                far_frame.node = plane.children[side ^ 1];
                far_frame.begin = split;
                far_frame.begin_node = node;
                far_frame.begin_side = side ^ 1;
                if( count >= WBSP_MAX_DEPTH ) return 0;
                stack[count++] = far_frame;
            }
            frame.node = plane.children[side];
            frame.end = split;
            frame.end_node = node;
            frame.end_side = side;
            if( frame.end <= frame.begin ) break;
        }
        if( frame.end <= frame.begin ) continue;
        if( frame.node >= 0 || frame.node < -19 || frame.node == -7 ||
            frame.node == -8 || ++visits > WBSP_MAX_VISITS ) return 0;
        if( frame.node == WBSP_SOLID_CONTENTS )
        {
            if( phase == 2 )
            {
                *out = interval;
                return 1;
            }
            if( phase == 0 )
            {
                if( !have_prefix || frame.begin_node < 0 ) return 0;
                interval.begin = frame.begin;
                interval.entry_node = frame.begin_node;
                interval.entry_side = frame.begin_side;
                phase = 1;
            }
            interval.end = frame.end;
            interval.exit_node = frame.end_node;
            interval.exit_side = frame.end_side;
        }
        else
        {
            if( phase == 0 ) have_prefix = 1;
            if( phase == 1 )
            {
                if( interval.exit_node < 0 || frame.begin != interval.end )
                    return 0;
                phase = 2;
            }
            if( phase == 2 ) interval.clear_end = frame.end;
        }
    }
    if( phase != 2 || interval.end <= interval.begin ||
        interval.clear_end <= interval.end )
        return 0;
    *out = interval;
    return 1;
}
#endif
