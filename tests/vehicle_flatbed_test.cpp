#include "cata_catch.h"
#include "avatar.h"
#include "avatar_action.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "debug.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "player_helpers.h"
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

TEST_CASE( "character_walks_onto_parked_flatbed_no_board_errors", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    clear_avatar();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );

    // Parked truck facing +x; ramp tail at the truck's -x end (world x = 60-2 = 58).
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    truck->velocity = 0;
    truck->engine_on = false;

    avatar &u = get_avatar();
    // Start on the ground just behind the ramp tail, facing the truck.
    u.setpos( here, tripoint_bub_ms( 57, 60, 0 ) );
    here.build_map_cache( 0, true );

    // Walk east across the whole truck: ground -> ramp -> bed frames -> seats -> off.
    std::string dmsg = capture_debugmsg_during( [&]() {
        for( int step = 0; step < 7; step++ ) {
            u.set_moves( 1000 );
            avatar_action::move( u, here, tripoint_rel_ms::east );
        }
    } );
    CAPTURE( dmsg );
    CHECK( dmsg.empty() );
}

TEST_CASE( "avatar_drives_car_up_ramp_then_walks_deck_no_board_errors", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    clear_avatar();
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

    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    truck->velocity = 0;
    truck->engine_on = false;

    vehicle *car = here.add_vehicle( vehicle_prototype_test_car,
                                     tripoint_bub_ms( 56, 60, 0 ), 0_degrees, 100, 0 );
    REQUIRE( car != nullptr );

    // Avatar rides the car up as its driver (real gameplay: you drive onto the bed).
    avatar &u = get_avatar();
    u.setpos( here, tripoint_bub_ms( 56, 60, 0 ) );
    here.board_vehicle( u.pos_bub( here ), &u );
    REQUIRE( u.in_vehicle );

    car->tags.insert( "IN_CONTROL_OVERRIDE" );
    car->engine_on = true;
    car->cruise_velocity = 200;
    car->velocity = 200;

    std::string dmsg = capture_debugmsg_during( [&]() {
        for( int cycle = 0; cycle < 20; cycle++ ) {
            here.vehmove();
            if( car->pos_bub( here ).z() == 1 ) {
                car->cruise_velocity = 0;
                car->velocity = 0;
                break;
            }
        }
        for( int i = 0; i < 3; i++ ) {
            here.vehmove();
        }
    } );
    CAPTURE( dmsg );
    CHECK( dmsg.empty() );

    // The avatar rode up with the car and is still on the same z-level as it.
    CAPTURE( u.pos_bub( here ).z() );
    CAPTURE( car->pos_bub( here ).z() );
    CHECK( u.pos_bub( here ).z() == car->pos_bub( here ).z() );

    // The avatar is boarded in the car at z+1. The board state the map resolves for the
    // avatar's tile must be self-consistent: veh_at() at the avatar's z+1 tile must find a
    // BOARDABLE part, and unboarding (what place_player does on every subsequent move) must
    // not raise "passenger not found" / "vehicle not found".
    CAPTURE( u.pos_bub( here ).to_string() );
    const optional_vpart_position vp = here.veh_at( u.pos_bub( here ) );
    REQUIRE( vp );
    CHECK( vp->part_with_feature( "BOARDABLE", true ).has_value() );
    std::string dmsg2 = capture_debugmsg_during( [&]() {
        here.unboard_vehicle( u.pos_bub( here ) );
    } );
    CAPTURE( dmsg2 );
    CHECK( dmsg2.empty() );

    // PROBE (scoping item 2): can the avatar STAND on the truck's own roof deck at z+1
    // (a tile with a roof part below, but no vehicle part at z+1 itself) without falling?
    // The bubble is already at z+1 from the drive-up's vertical_move, so setpos sticks.
    // Find a truck roof-deck tile at z+1 not occupied by the car.
    creature_tracker &cr = get_creature_tracker();
    std::optional<tripoint_bub_ms> roof_deck;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( truck->roof_at_part( vpr.part_index() ) < 0 ) {
            continue; // this ground part has no roof above it
        }
        const tripoint_bub_ms above = vpr.pos_bub( here ) + tripoint_rel_ms( 0, 0, 1 );
        if( cr.creature_at( above ) == nullptr && !here.veh_at( above ) ) {
            roof_deck = above;
            break;
        }
    }
    REQUIRE( roof_deck.has_value() );
    CAPTURE( roof_deck->to_string() );
    CAPTURE( here.is_open_air( *roof_deck ) );          // deck tile itself
    CAPTURE( here.has_floor( *roof_deck ) );            // terrain floor?
    // valid_move down from the deck tile: false == supported (won't fall).
    const tripoint_bub_ms below = *roof_deck + tripoint_rel_ms( 0, 0, -1 );
    CAPTURE( here.valid_move( *roof_deck, below, false, true ) );
    CAPTURE( here.has_floor_or_support( *roof_deck ) );

    u.setpos( here, *roof_deck );
    REQUIRE( u.pos_bub( here ) == *roof_deck );
    u.gravity_check( &here );
    // Standing on the deck already works: the roof part below counts as support in the
    // character fall path (map::valid_move -> roof_at_part), so the avatar does NOT fall.
    CHECK( u.pos_bub( here ).z() == 1 );
    // NOTE (documents the item-2 gap, not yet fixed): although the deck tile is supported,
    // it is is_open_air()==true with no vehicle part at z+1, so game::walk_move classifies a
    // step onto/across it as a "ledge" and opens the climb-down menu. Walking the deck is the
    // work of the walkable-carrier-deck feature; a regression test for it lands with that fix.
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

TEST_CASE( "test_flatbed_bed_has_walkable_roof_deck", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    // At least one bed part carries WALKABLE_ROOF, and it is a roof (provides fall support).
    bool found_walkable_deck = false;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( vpr.info().has_flag( VPFLAG_WALKABLE_ROOF ) ) {
            found_walkable_deck = true;
            CHECK( truck->roof_at_part( vpr.part_index() ) >= 0 );
        }
    }
    CHECK( found_walkable_deck );
}

TEST_CASE( "avatar_walks_across_flatbed_deck_no_ledge_menu", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    clear_avatar();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );

    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    truck->velocity = 0;
    truck->engine_on = false;
    vehicle *car = here.add_vehicle( vehicle_prototype_test_car,
                                     tripoint_bub_ms( 56, 60, 0 ), 0_degrees, 100, 0 );
    REQUIRE( car != nullptr );

    avatar &u = get_avatar();
    u.setpos( here, tripoint_bub_ms( 56, 60, 0 ) );
    here.board_vehicle( u.pos_bub( here ), &u );
    REQUIRE( u.in_vehicle );
    car->tags.insert( "IN_CONTROL_OVERRIDE" );
    car->engine_on = true;
    car->cruise_velocity = 200;
    car->velocity = 200;
    for( int cycle = 0; cycle < 20; cycle++ ) {
        here.vehmove();
        if( car->pos_bub( here ).z() == 1 ) {
            car->cruise_velocity = 0;
            car->velocity = 0;
            break;
        }
    }
    for( int i = 0; i < 3; i++ ) {
        here.vehmove();
    }
    REQUIRE( u.pos_bub( here ).z() == 1 );

    // Step off the car onto the truck's own deck, then across it.
    creature_tracker &cr = get_creature_tracker();
    here.unboard_vehicle( u.pos_bub( here ) );
    std::optional<tripoint_bub_ms> roof_deck;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( truck->roof_at_part( vpr.part_index() ) < 0 ) {
            continue;
        }
        const tripoint_bub_ms above = vpr.pos_bub( here ) + tripoint_rel_ms( 0, 0, 1 );
        if( cr.creature_at( above ) == nullptr && !here.veh_at( above ) ) {
            roof_deck = above;
            break;
        }
    }
    REQUIRE( roof_deck.has_value() );
    u.setpos( here, *roof_deck );
    REQUIRE( u.pos_bub( here ) == *roof_deck );

    // Find an adjacent roof-supported deck tile and walk to it.
    std::optional<tripoint_rel_ms> across_dir;
    for( const tripoint_rel_ms &d : { tripoint_rel_ms( 1, 0, 0 ), tripoint_rel_ms( -1, 0, 0 ),
                                      tripoint_rel_ms( 0, 1, 0 ), tripoint_rel_ms( 0, -1, 0 ) } ) {
        const tripoint_bub_ms nb = *roof_deck + d;
        if( here.deck_floor_below( nb ) && !here.veh_at( nb ) && cr.creature_at( nb ) == nullptr ) {
            across_dir = d;
            break;
        }
    }
    REQUIRE( across_dir.has_value() );
    const tripoint_abs_ms across_target_abs = here.get_abs( *roof_deck + *across_dir );
    u.set_moves( 1000 );
    std::string dmsg = capture_debugmsg_during( [&]() {
        avatar_action::move( u, here, *across_dir );
    } );
    CAPTURE( dmsg );
    CHECK( dmsg.empty() );
    CHECK( u.pos_abs() == across_target_abs );  // actually walked across, no menu/fall
}

TEST_CASE( "deck_floor_below_true_only_over_walkable_roof", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    // open air above z0 so deck tiles are is_open_air
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );

    // A tile above a WALKABLE_ROOF bed part -> true.
    std::optional<tripoint_bub_ms> deck_tile;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( vpr.info().has_flag( VPFLAG_WALKABLE_ROOF ) ) {
            deck_tile = vpr.pos_bub( here ) + tripoint_rel_ms( 0, 0, 1 );
            break;
        }
    }
    REQUIRE( deck_tile.has_value() );
    CHECK( here.deck_floor_below( *deck_tile ) );

    // A z+1 tile far from the truck (open air over bare pavement) -> false.
    CHECK_FALSE( here.deck_floor_below( tripoint_bub_ms( 40, 40, 1 ) ) );
    // The deck part's own z0 tile is not open air-above-a-deck -> false.
    CHECK_FALSE( here.deck_floor_below( *deck_tile + tripoint_rel_ms( 0, 0, -1 ) ) );
}
