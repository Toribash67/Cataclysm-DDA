#include "cata_catch.h"
#include "coordinates.h"
#include "map.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "point.h"
#include "type_id.h"
#include "vehicle.h"

static const vproto_id vehicle_prototype_car( "car" );

// Build a field where z0 is solid pavement and z1 is entirely open air,
// so the ONLY possible support at z1 is a vehicle sitting at z0 below.
static void build_open_air_upper( map &here )
{
    build_test_map( ter_id( "t_pavement" ) );
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 0 ), ter_id( "t_pavement" ) );
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );
}

TEST_CASE( "vehicle_rests_on_vehicle_below_is_supported", "[vehicle][flatbed][multifloor]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_open_air_upper( here );

    const tripoint_bub_ms lower_pos( 60, 60, 0 );
    const tripoint_bub_ms upper_pos( 60, 60, 1 );

    // "truck" stand-in at z0 (any vehicle provides veh_at() support above it)
    vehicle *lower = here.add_vehicle( vehicle_prototype_car, lower_pos, 0_degrees, 0, 0 );
    REQUIRE( lower != nullptr );
    // "carried car" directly above at z1, over open air, supported only by `lower`
    vehicle *upper = here.add_vehicle( vehicle_prototype_car, upper_pos, 0_degrees, 0, 0 );
    REQUIRE( upper != nullptr );

    upper->check_falling_or_floating();
    CHECK_FALSE( upper->is_falling );
}
