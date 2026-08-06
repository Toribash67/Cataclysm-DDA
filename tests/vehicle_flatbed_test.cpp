#include "cata_catch.h"
#include "coordinates.h"
#include "map.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "point.h"
#include "type_id.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"

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

static const vproto_id vehicle_prototype_test_flatbed_truck( "test_flatbed_truck" );
static const vproto_id vehicle_prototype_test_car( "test_car" );

TEST_CASE( "test_flatbed_truck_has_a_ramp_part", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    bool found_ramp = false;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( vpr.info().has_flag( VPFLAG_VEH_RAMP_UP ) ) {
            found_ramp = true;
        }
    }
    CHECK( found_ramp );
}

TEST_CASE( "test_car_drives_up_ramp_onto_parked_truck_bed", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    // open air above z0 so the bed's support must come from the truck
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );

    // Parked truck facing +x; its ramp tail is at the truck's -x end.
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    truck->velocity = 0;
    truck->engine_on = false;

    // Car placed just behind the ramp tail, facing +x (toward the truck).
    vehicle *car = here.add_vehicle( vehicle_prototype_test_car,
                                     tripoint_bub_ms( 56, 60, 0 ), 0_degrees, 100, 0 );
    REQUIRE( car != nullptr );
    car->tags.insert( "IN_CONTROL_OVERRIDE" );
    car->engine_on = true;
    car->cruise_velocity = 200;
    car->velocity = 200;

    // Drive forward until the car has climbed; a handful of ticks suffices.
    // Drive forward until the car's reference position actually climbs a
    // z-level, then stop it so it settles on the bed rather than driving off.
    for( int cycle = 0; cycle < 20; cycle++ ) {
        here.vehmove();
        if( car->pos_bub( here ).z() == 1 ) {
            car->cruise_velocity = 0;
            car->velocity = 0;
            break;
        }
    }
    // let it settle in place
    for( int i = 0; i < 3; i++ ) {
        here.vehmove();
    }

    // The whole car actually transitioned up a z-level (its reference position
    // moved, not just one part's precalc), and it is not split across levels.
    const int car_z = car->pos_bub( here ).z();
    CAPTURE( car_z );
    CHECK( car_z == 1 );
    int minz = 100;
    int maxz = -100;
    for( const vpart_reference &vpr : car->get_all_parts() ) {
        const vehicle_part &vp = vpr.part();
        if( vp.removed || vp.is_fake ) {
            continue;
        }
        const int z = vpr.pos_bub( here ).z();
        minz = std::min( minz, z );
        maxz = std::max( maxz, z );
    }
    CAPTURE( minz );
    CAPTURE( maxz );
    CHECK( minz == 1 ); // every real part is up a level
    CHECK( maxz == 1 ); // and none left behind on the ground (not split)
    // ...it did not fall...
    car->check_falling_or_floating();
    CHECK_FALSE( car->is_falling );
    // ...and it is still a SEPARATE vehicle (M1 boundary: no merge).
    CHECK( car != truck );
    bool car_has_carried = false;
    for( const vpart_reference &vpr : car->get_all_parts() ) {
        if( vpr.part().has_flag( vp_flag::carried_flag ) ) {
            car_has_carried = true;
        }
    }
    CHECK_FALSE( car_has_carried );
}
