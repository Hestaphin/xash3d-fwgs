#ifndef WORLD_BALLISTIC_QUERY_IMPL_H
#define WORLD_BALLISTIC_QUERY_IMPL_H
/* J.10.6.1: exact world intervals, exported as an optional native object.
 * The frozen physics table is unchanged. World geometry and QBSP2 layout
 * stay inside the engine rather than being cast through the game DLL.
 */
typedef struct world_ballistic_context_s
{
    model_t *model;
    hull_t *hull;
} world_ballistic_context_t;

static int SV_ReadWorldBallisticNode( void *opaque, int index, wbsp_node_t *out )
{
    world_ballistic_context_t *context = opaque;
    hull_t *hull = context->hull;
    model_t *model = context->model;
    int plane_index, i;
    if( index < hull->firstclipnode || index > hull->lastclipnode ||
        index >= model->numnodes )
        return 0;
    if( world.version == QBSP2_VERSION )
    {
        if( !hull->clipnodes32 ) return 0;
        plane_index = hull->clipnodes32[index].planenum;
        out->children[0] = hull->clipnodes32[index].children[0];
        out->children[1] = hull->clipnodes32[index].children[1];
    }
    else
    {
        if( !hull->clipnodes16 ) return 0;
        plane_index = hull->clipnodes16[index].planenum;
        out->children[0] = hull->clipnodes16[index].children[0];
        out->children[1] = hull->clipnodes16[index].children[1];
    }
    if( plane_index < 0 || plane_index >= model->numplanes ) return 0;
    out->distance = hull->planes[plane_index].dist;
    for( i = 0; i < 3; ++i )
        out->normal[i] = hull->planes[plane_index].normal[i];
    return 1;
}

/* Resolve the actual face at the interval's boundary node. Do not reverse
 * trace from a guessed distant point or let another plane supply its face.
 * Coincident/shared-edge faces are conservative failures.
 */
static msurface_t *SV_WorldBallisticSurface( model_t *model, int node_index,
    const vec3_t point )
{
    msurface_t *found = NULL;
    mnode_t *node;
    int first, count, i;
    if( node_index < 0 || node_index >= model->numnodes ) return NULL;
    node = &model->nodes[node_index];
    if( !node->plane || fabs( PlaneDiff( point, node->plane )) > 0.01 )
        return NULL;
    first = node_firstsurface( node, model );
    count = node_numsurfaces( node, model );
    if( first < 0 || count < 0 || first > model->numsurfaces ||
        count > model->numsurfaces - first )
        return NULL;
    for( i = 0; i < count; ++i )
    {
        msurface_t *surface = &model->surfaces[first + i];
        mfacebevel_t *bevel = surface->info ? surface->info->bevel : NULL;
        vec3_t delta;
        int edge;
        if( !bevel || !bevel->edges || bevel->numedges <= 0 ||
            bevel->numedges > 4096 || !WBSP_Finite( bevel->radius ) ||
            bevel->radius <= 0 )
            return NULL;
        VectorSubtract( point, bevel->origin, delta );
        if( !WBSP_Finite( DotProduct( delta, delta ))) return NULL;
        if( DotProduct( delta, delta ) > bevel->radius ) continue;
        for( edge = 0; edge < bevel->numedges; ++edge )
        {
            double distance = PlaneDiff( point, &bevel->edges[edge] );
            if( !WBSP_Finite( distance )) return NULL;
            if( distance > FRAC_EPSILON ) break;
        }
        if( edge != bevel->numedges ) continue;
        if( surface->plane != node->plane ||
            !surface->texinfo || !surface->texinfo->texture ||
            PM_SampleMiptex( surface, point ) == CONTENTS_EMPTY )
            return NULL;
        if( found && found != surface ) return NULL;
        found = surface;
    }
    return found;
}

static int GAME_EXPORT SV_QueryWorldBallisticInterval( const world_ballistic_request_t *request,
    world_ballistic_result_t *result )
{
    world_ballistic_context_t context;
    wbsp_interval_t interval;
    wbsp_node_t entry_plane, exit_plane;
    double origin[3], direction[3], norm = 0, length;
    vec3_t offset;
    edict_t *entity;
    int i;
    if( !request || !result || request->struct_size < sizeof( *request ) ||
        result->struct_size < sizeof( *result ))
        return 0;
    memset( result, 0, sizeof( *result ));
    result->struct_size = sizeof( *result );
    if( sv.state != ss_active || !sv.worldmodel ||
        !WBSP_Finite( request->contact_distance ) || request->contact_distance < 0 ||
        request->contact_distance > 8192 ||
        !WBSP_Finite( request->maximum_depth ) || request->maximum_depth <= 0 ||
        request->maximum_depth > 96 ||
        !WBSP_Finite( request->preferred_offset ) || request->preferred_offset <= 0 ||
        request->preferred_offset > 0.5 )
        return 0;
    for( i = 0; i < 3; ++i )
    {
        origin[i] = request->start[i];
        direction[i] = request->direction[i];
        if( !WBSP_Finite( origin[i] ) || !WBSP_Finite( direction[i] )) return 0;
        norm += direction[i] * direction[i];
    }
    if( fabs( norm - 1.0 ) > 0.001 ) return 0;
    norm = sqrt( norm );
    for( i = 0; i < 3; ++i ) direction[i] /= norm;
    entity = SV_EdictNum( 0 );
    context.model = sv.worldmodel;
    if( !entity || !VectorIsNull( entity->v.origin ) ||
        !VectorIsNull( entity->v.angles ) ||
        context.model != SV_ModelHandle( entity->v.modelindex ) ||
        context.model->type != mod_brush || !context.model->nodes ||
        !context.model->surfaces || !context.model->planes )
        return 0;
    context.hull = SV_HullForBsp( entity, vec3_origin, vec3_origin, offset );
    if( context.hull != &context.model->hulls[0] || !VectorIsNull( offset ) ||
        !context.hull->planes || context.hull->firstclipnode < 0 ||
        context.hull->lastclipnode >= context.model->numnodes )
        return 0;
    length = request->contact_distance + request->maximum_depth +
        request->preferred_offset;
    if( !WBSP_FirstSolidInterval( &context, SV_ReadWorldBallisticNode,
            context.hull->firstclipnode, origin, direction, length, &interval ) ||
        fabs( interval.begin - request->contact_distance ) > 0.25 ||
        interval.end <= request->contact_distance ||
        interval.end - request->contact_distance > request->maximum_depth ||
        !SV_ReadWorldBallisticNode( &context, interval.entry_node, &entry_plane ) ||
        !SV_ReadWorldBallisticNode( &context, interval.exit_node, &exit_plane ))
        return 0;
    result->entry_distance = interval.begin;
    result->exit_distance = interval.end;
    result->clear_end_distance = interval.clear_end;
    for( i = 0; i < 3; ++i )
    {
        result->entry_position[i] = origin[i] + direction[i] * interval.begin;
        result->exit_position[i] = origin[i] + direction[i] * interval.end;
        result->entry_normal[i] = entry_plane.normal[i] *
            ( interval.entry_side ? 1.0 : -1.0 );
        result->exit_normal[i] = exit_plane.normal[i] *
            ( interval.exit_side ? 1.0 : -1.0 );
    }
    result->entry_surface = SV_WorldBallisticSurface( context.model,
        interval.entry_node, result->entry_position );
    result->exit_surface = SV_WorldBallisticSurface( context.model,
        interval.exit_node, result->exit_position );
    return result->entry_surface && result->exit_surface;
}

const world_ballistic_query_api_t *SV_GetWorldBallisticQuery( void )
{
    static const world_ballistic_query_api_t api = {
        WORLD_BALLISTIC_QUERY_VERSION, sizeof( world_ballistic_query_api_t ),
        SV_QueryWorldBallisticInterval
    };
    return &api;
}

#endif
