#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "tileray.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"

static const mtype_id pseudo_debug_mon( "pseudo_debug_mon" );

static const vproto_id vehicle_prototype_test_bus_2floor( "test_bus_2floor" );

static void clear_game_and_set_ramp( const int transit_x, bool use_ramp, bool up )
{
    // Set to turn 0 to prevent solars from producing power
    calendar::turn = calendar::turn_zero;
    clear_map();
    clear_vehicles();

    Character &player_character = get_player_character();
    // Move player somewhere safe
    REQUIRE_FALSE( player_character.in_vehicle );

    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    if( use_ramp ) {
        const int upper_zlevel = up ? 1 : 0;
        const int lower_zlevel = up - 1;
        const int highx = transit_x + ( up ? 0 : 1 );
        const int lowx = transit_x + ( up ? 1 : 0 );

        // up   z1    ......  rdh  rDl
        //      z0            rUh  rul .................
        // down z0            rDl  rdh .................
        //      z-1   ......  rdl  rUh
        //                    60   61
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            for( int x = 0; x < transit_x; x++ ) {
                const int mid = up ? upper_zlevel : lower_zlevel;
                here.ter_set( tripoint_bub_ms( x, y, mid - 2 ), ter_id( "t_rock" ) );
                here.ter_set( tripoint_bub_ms( x, y, mid - 1 ), ter_id( "t_rock" ) );
                here.ter_set( tripoint_bub_ms( x, y, mid ), ter_id( "t_pavement" ) );
                here.ter_set( tripoint_bub_ms( x, y, mid + 1 ), ter_id( "t_open_air" ) );
                here.ter_set( tripoint_bub_ms( x, y, mid + 2 ), ter_id( "t_open_air" ) );
            }
            const tripoint_bub_ms ramp_up_low = tripoint_bub_ms( lowx, y, lower_zlevel );
            const tripoint_bub_ms ramp_up_high = tripoint_bub_ms( highx, y, lower_zlevel );
            const tripoint_bub_ms ramp_down_low = tripoint_bub_ms( lowx, y, upper_zlevel );
            const tripoint_bub_ms ramp_down_high = tripoint_bub_ms( highx, y, upper_zlevel );
            here.ter_set( ramp_up_low, ter_id( "t_ramp_up_low" ) );
            here.ter_set( ramp_up_high, ter_id( "t_ramp_up_high" ) );
            here.ter_set( ramp_down_low, ter_id( "t_ramp_down_low" ) );
            here.ter_set( ramp_down_high, ter_id( "t_ramp_down_high" ) );
            for( int x = transit_x + 2; x < SEEX * MAPSIZE; x++ ) {
                here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
                here.ter_set( tripoint_bub_ms( x, y, 0 ), ter_id( "t_pavement" ) );
                here.ter_set( tripoint_bub_ms( x, y, -1 ), ter_id( "t_rock" ) );
            }
        }
    }
    here.invalidate_map_cache( 0 );
    here.build_map_cache( 0, true );
}

// M5 Task 4: flat driving, easy case first. Confirms the two-floor bus drives
// under its own power on flat pavement without splitting, skidding, or losing
// velocity, and that the upper deck stays exactly one z above the lower deck
// at every tick (composition of precalc.z = mount.z + precalc_z_delta holds
// with no ramp displacement in play).
TEST_CASE( "two_floor_bus_drives_flat_keeping_deck_stack", "[vehicle][multifloor][ramp]" )
{
    map &here = get_map();
    clear_game_and_set_ramp( 75, /*use_ramp=*/false, /*up=*/false ); // flat pavement field

    const tripoint_bub_ms start( 79, 60, 0 );
    REQUIRE( here.ter( start ) == ter_id( "t_pavement" ) );
    vehicle *veh_ptr = here.add_vehicle( vehicle_prototype_test_bus_2floor, start, 180_degrees, 1, 0 );
    REQUIRE( veh_ptr != nullptr );
    vehicle &veh = *veh_ptr;
    veh.check_falling_or_floating();
    REQUIRE_FALSE( veh.is_in_water() );

    veh.tags.insert( "IN_CONTROL_OVERRIDE" );
    veh.engine_on = true;
    const int target_velocity = 400;
    veh.cruise_velocity = target_velocity;
    veh.velocity = target_velocity;
    REQUIRE( veh.safe_velocity( here ) > 0 );

    const int parts_before = veh.part_count();

    for( int cycle = 0; cycle < 8; cycle++ ) {
        CAPTURE( cycle );
        here.vehmove();
        REQUIRE_FALSE( veh.skidding );
        CHECK( veh.velocity == target_velocity );
        // the vehicle stays whole (no spurious split while driving)
        CHECK( veh.part_count() == parts_before );
        // every upper-deck part is exactly one z above its lower-deck column partner,
        // and all lower-deck structure stays at the vehicle's own z-level.
        for( const vpart_reference &up : veh.get_all_parts() ) {
            const vehicle_part &pu = up.part();
            if( pu.removed || pu.is_fake || pu.mount.z() != 1 ) {
                continue;
            }
            for( const vpart_reference &lo : veh.get_all_parts() ) {
                const vehicle_part &pl = lo.part();
                if( pl.removed || pl.is_fake || pl.mount.z() != 0 ||
                    pl.mount.xy() != pu.mount.xy() ) {
                    continue;
                }
                CAPTURE( pu.mount.xy() );
                CHECK( pu.precalc[0].z() == pl.precalc[0].z() + 1 );
            }
        }
    }
}

// M5 Task 5: drive the 2-floor bus over a ramp. Mid-transition the bus spans
// THREE z-levels at once (rear lower deck z0, rear upper / front lower z1,
// front upper z2) -- the roughest existing movement path. Confirms the deck
// stack invariant (upper precalc.z == lower precalc.z + 1, per column) holds
// throughout, the bus stays whole (no spurious split), and it actually
// changes z-level.
static void drive_two_floor_bus_ramp( bool up )
{
    map &here = get_map();
    const int transition_x = 60;
    clear_game_and_set_ramp( transition_x, /*use_ramp=*/true, up );

    const tripoint_bub_ms start( transition_x + 4, 60, up ? 0 : 1 );
    REQUIRE( here.ter( start ) ); // terrain present at the start z
    vehicle *veh_ptr = here.add_vehicle( vehicle_prototype_test_bus_2floor, start, 180_degrees, 1, 0 );
    REQUIRE( veh_ptr != nullptr );
    vehicle &veh = *veh_ptr;
    veh.check_falling_or_floating();
    REQUIRE_FALSE( veh.is_in_water() );

    veh.tags.insert( "IN_CONTROL_OVERRIDE" );
    veh.engine_on = true;
    const int target_velocity = 400;
    veh.cruise_velocity = target_velocity;
    veh.velocity = target_velocity;
    REQUIRE( veh.safe_velocity( here ) > 0 );

    const int parts_before = veh.part_count();
    bool saw_base_z_change = false;
    const int base_z_start = veh.sm_pos.z();

    for( int cycle = 0; cycle < 10 && veh.safe_velocity( here ) > 0; cycle++ ) {
        CAPTURE( cycle );
        clear_creatures();
        here.vehmove();
        REQUIRE_FALSE( veh.skidding );
        // stays whole across the transition (no split when spanning 3 z-levels)
        CHECK( veh.part_count() == parts_before );
        // deck stack invariant holds even mid-ramp, per column
        for( const vpart_reference &pref : veh.get_all_parts() ) {
            const vehicle_part &pu = pref.part();
            if( pu.removed || pu.is_fake || pu.mount.z() != 1 ) {
                continue;
            }
            for( const vpart_reference &lref : veh.get_all_parts() ) {
                const vehicle_part &pl = lref.part();
                if( pl.removed || pl.is_fake || pl.mount.z() != 0 ||
                    pl.mount.xy() != pu.mount.xy() ) {
                    continue;
                }
                CAPTURE( pu.mount.xy() );
                CHECK( pu.precalc[0].z() == pl.precalc[0].z() + 1 );
            }
        }
        if( veh.sm_pos.z() != base_z_start ) {
            saw_base_z_change = true;
        }
    }
    // the bus actually changed z-level over the ramp
    CHECK( saw_base_z_change );
    CHECK( veh.part_count() == parts_before );
}

TEST_CASE( "two_floor_bus_drives_up_a_ramp", "[vehicle][multifloor][ramp]" )
{
    drive_two_floor_bus_ramp( /*up=*/true );
}

TEST_CASE( "two_floor_bus_drives_down_a_ramp", "[vehicle][multifloor][ramp]" )
{
    drive_two_floor_bus_ramp( /*up=*/false );
}

// M5 Task 5 Part B: a rider boarded on an UPPER-deck seat must track the upper
// deck through a ramp z-transition, not the ground deck and not a desynced
// level. map::displace_vehicle's passenger-placement ramp probe (src/map.cpp,
// around line 1610) is a known twin of the advance_precalc_mounts bug fixed in
// M5 Task 3: it probes the ramp flag at the ridden part's OWN z instead of its
// ground-deck column tile, so an upper-deck passenger reads the paired (and
// opposite) ramp terrain a z-level away and gets displaced the wrong way.
TEST_CASE( "two_floor_bus_upper_deck_rider_rides_over_ramp", "[vehicle][multifloor][ramp]" )
{
    map &here = get_map();
    const int transition_x = 60;
    const bool up = true;
    clear_game_and_set_ramp( transition_x, /*use_ramp=*/true, up );

    const tripoint_bub_ms start( transition_x + 4, 60, up ? 0 : 1 );
    REQUIRE( here.ter( start ) ); // terrain present at the start z
    vehicle *veh_ptr = here.add_vehicle( vehicle_prototype_test_bus_2floor, start, 180_degrees, 1, 0 );
    REQUIRE( veh_ptr != nullptr );
    vehicle &veh = *veh_ptr;
    veh.check_falling_or_floating();
    REQUIRE_FALSE( veh.is_in_water() );

    veh.tags.insert( "IN_CONTROL_OVERRIDE" );
    veh.engine_on = true;
    const int target_velocity = 400;
    veh.cruise_velocity = target_velocity;
    veh.velocity = target_velocity;
    REQUIRE( veh.safe_velocity( here ) > 0 );

    // Board a rider on the upper-deck seat: climb through the ground connector, then
    // step onto the adjacent upper-deck seat -- exactly as
    // vehicle_multifloor_test.cpp's avatar_boards_upper_deck_seat does. A bare setpos
    // to a z=1 tile cannot be boarded reliably in a test (board_vehicle's trailing
    // update_map() re-centres the reality bubble on the argless Character::pos_bub(),
    // which only the bubble-shifting vertical_move path refreshes), so climb via
    // try_vehicle_deck_move instead.
    clear_avatar();
    avatar &u = get_avatar();
    const int connector = veh.part_with_feature( tripoint_rel_ms( 0, 0, 0 ), "VERTICAL_TRAVERSAL",
                          false );
    REQUIRE( connector >= 0 );
    const tripoint_bub_ms connector_pos = veh.bub_part_pos( here, veh.part( connector ) );
    u.setpos( here, connector_pos );
    here.board_vehicle( connector_pos, &u );
    REQUIRE( u.in_vehicle );
    REQUIRE( u.pos_bub( here ).z() == connector_pos.z() );

    REQUIRE( g->try_vehicle_deck_move( 1 ) );
    REQUIRE( u.pos_bub( here ).z() == connector_pos.z() + 1 );

    const int upper_seat = veh.part_with_feature( tripoint_rel_ms( 0, 1, 1 ), "SEAT", false );
    REQUIRE( upper_seat >= 0 );
    const tripoint_bub_ms seat_pos = veh.bub_part_pos( here, veh.part( upper_seat ) );
    REQUIRE( seat_pos.z() == 1 );
    here.unboard_vehicle( u.pos_bub( here ) );
    u.setpos( here, seat_pos );
    here.board_vehicle( seat_pos, &u );
    REQUIRE( u.in_vehicle );
    REQUIRE( u.pos_bub( here ).z() == seat_pos.z() );

    const int lower_seat = veh.part_with_feature( tripoint_rel_ms( 0, 1, 0 ), "SEAT", false );
    REQUIRE( lower_seat >= 0 );

    // Drive over the ramp and confirm the rider tracks the upper deck: exactly one z
    // above the ground-deck seat in the same column, at every tick through the
    // transition (never equal to the ground deck's z, never desynced by more than 1).
    bool saw_z_change = false;
    const int rider_z_start = u.pos_bub( here ).z();
    for( int cycle = 0; cycle < 10 && veh.safe_velocity( here ) > 0; cycle++ ) {
        CAPTURE( cycle );
        clear_creatures();
        here.vehmove();
        REQUIRE_FALSE( veh.skidding );
        const int ground_z = veh.bub_part_pos( here, veh.part( lower_seat ) ).z();
        CAPTURE( ground_z );
        CAPTURE( u.pos_bub( here ) );
        CHECK( u.pos_bub( here ).z() == ground_z + 1 );
        if( u.pos_bub( here ).z() != rider_z_start ) {
            saw_z_change = true;
        }
    }
    CHECK( saw_z_change );
}

// Algorithm goes as follows:
// Clear map and create a ramp
// Spawn a vehicle
// Drive it over the ramp, and confirm that the vehicle changes z-levels
static void ramp_transition_angled( const vproto_id &veh_id, const units::angle angle,
                                    const int transition_x, bool use_ramp, bool up )
{
    map &here = get_map();
    clear_game_and_set_ramp( transition_x, use_ramp, up );

    const tripoint_bub_ms map_starting_point( transition_x + 4, 60, 0 );
    REQUIRE( here.ter( map_starting_point ) == ter_id( "t_pavement" ) );
    if( here.ter( map_starting_point ) != ter_id( "t_pavement" ) ) {
        return;
    }
    vehicle *veh_ptr = here.add_vehicle( veh_id, map_starting_point, angle, 1, 0 );

    REQUIRE( veh_ptr != nullptr );
    if( veh_ptr == nullptr ) {
        return;
    }

    vehicle &veh = *veh_ptr;
    veh.check_falling_or_floating();

    REQUIRE( !veh.is_in_water() );

    veh.tags.insert( "IN_CONTROL_OVERRIDE" );
    veh.engine_on = true;
    Character &player_character = get_player_character();
    player_character.setpos( here, map_starting_point );

    REQUIRE( player_character.pos_bub( here ) == map_starting_point );
    if( player_character.pos_bub( here ) != map_starting_point ) {
        return;
    }
    get_map().board_vehicle( map_starting_point, &player_character );
    REQUIRE( player_character.pos_bub( here ) == map_starting_point );
    if( player_character.pos_bub( here ) != map_starting_point ) {
        return;
    }
    const int transition_cycle = 3;
    veh.cruise_velocity = 0;
    veh.velocity = 0;
    here.vehmove();

    const int target_velocity = 400;
    veh.cruise_velocity = target_velocity;
    veh.velocity = target_velocity;
    CHECK( veh.safe_velocity( here ) > 0 );
    int cycles = 0;
    const int target_z = use_ramp ? ( up ? 1 : -1 ) : 0;

    std::set<tripoint_abs_ms> vpts = veh.get_points();
    while( veh.engine_on && veh.safe_velocity( here ) > 0 && cycles < 10 ) {
        clear_creatures();
        CAPTURE( cycles );
        for( const tripoint_abs_ms &checkpt : vpts ) {
            int partnum = 0;
            vehicle *check_veh = here.veh_at_internal( here.get_bub( checkpt ), partnum );
            CAPTURE( veh_ptr->pos_bub( here ) );
            CAPTURE( veh_ptr->face.dir() );
            CAPTURE( checkpt );
            CHECK( check_veh == veh_ptr );
        }
        vpts.clear();
        here.vehmove();
        CHECK( veh.velocity == target_velocity );
        // If the vehicle starts skidding, the effects become random and test is RUINED
        REQUIRE( !veh.skidding );
        for( const tripoint_abs_ms &pos : veh.get_points() ) {
            REQUIRE( here.ter( here.get_bub( pos ) ) );
        }
        for( const vpart_reference &vp : veh.get_all_parts() ) {
            if( vp.info().location != "structure" ) {
                continue;
            }
            const point_rel_ms &pmount = vp.mount_pos();
            CAPTURE( pmount );
            const tripoint_bub_ms &ppos = vp.pos_bub( here );
            CAPTURE( ppos );
            if( cycles > ( transition_cycle - pmount.x() ) ) {
                CHECK( ppos.z() == target_z );
            } else {
                CHECK( ppos.z() == 0 );
            }
            if( pmount.x() == 0 && pmount.y() == 0 ) {
                CHECK( player_character.pos_bub() == ppos );
            }
        }
        vpts = veh.get_points();
        cycles++;
    }
    const std::optional<vpart_reference> vp = here.veh_at(
                player_character.pos_bub() ).part_with_feature(
                VPFLAG_BOARDABLE, true );
    REQUIRE( vp );
    if( vp ) {
        const int z_change = map_starting_point.z() - player_character.posz();
        here.unboard_vehicle( *vp, &player_character, false );
        here.ter_set( map_starting_point, ter_id( "t_pavement" ) );
        player_character.setpos( here, map_starting_point );
        if( z_change ) {
            g->vertical_move( z_change, true );
        }
    }
}

static void test_ramp( const std::string &type, const int transition_x )
{
    CAPTURE( type );
    SECTION( "no ramp" ) {
        ramp_transition_angled( vproto_id( type ), 180_degrees, transition_x, false, false );
    }
    SECTION( "ramp up" ) {
        ramp_transition_angled( vproto_id( type ), 180_degrees, transition_x, true, true );
    }
    SECTION( "ramp down" ) {
        ramp_transition_angled( vproto_id( type ), 180_degrees, transition_x, true, false );
    }
    SECTION( "angled no ramp" ) {
        ramp_transition_angled( vproto_id( type ), 225_degrees, transition_x, false, false );
    }
    SECTION( "angled ramp down" ) {
        ramp_transition_angled( vproto_id( type ), 225_degrees, transition_x, true, false );
    }
    SECTION( "angled ramp up" ) {
        ramp_transition_angled( vproto_id( type ), 225_degrees, transition_x, true, true );
    }
}

static std::vector<std::string> ramp_vehs_to_test = {{
        "motorcycle",
    }
};

// I'd like to do this in a single loop, but that doesn't work for some reason
TEST_CASE( "vehicle_ramp_test_59", "[vehicle][ramp]" )
{
    for( const std::string &veh : ramp_vehs_to_test ) {
        test_ramp( veh, 59 );
    }
}
TEST_CASE( "vehicle_ramp_test_60", "[vehicle][ramp]" )
{
    for( const std::string &veh : ramp_vehs_to_test ) {
        test_ramp( veh, 60 );
    }
}
TEST_CASE( "vehicle_ramp_test_61", "[vehicle][ramp]" )
{
    for( const std::string &veh : ramp_vehs_to_test ) {
        test_ramp( veh, 61 );
    }
}

static void level_out( const vproto_id &veh_id, const bool drop_pos )
{
    map &here = get_map();
    clear_game_and_set_ramp( 75, drop_pos, false );
    const int start_z = drop_pos ? 1 : 0;

    const tripoint_bub_ms map_starting_point( 60, 60, start_z );

    // Make sure the avatar is out of the way
    Character &player_character = get_player_character();
    player_character.setpos( here, map_starting_point + point( 5, 5 ) );
    vehicle *veh_ptr = here.add_vehicle( veh_id, map_starting_point, 180_degrees, 1, 0 );

    REQUIRE( veh_ptr != nullptr );
    if( veh_ptr == nullptr ) {
        return;
    }
    vehicle &veh = *veh_ptr;
    veh.check_falling_or_floating();

    REQUIRE( !veh.is_in_water() );

    veh.tags.insert( "IN_CONTROL_OVERRIDE" );
    veh.engine_on = true;

    const int target_velocity = 800;
    veh.cruise_velocity = target_velocity;
    veh.velocity = target_velocity;
    CHECK( veh.safe_velocity( here ) > 0 );

    std::vector<vehicle_part *> all_parts;
    for( const tripoint_abs_ms &pos : veh.get_points() ) {
        for( vehicle_part *prt : veh.get_parts_at( pos, "", part_status_flag::any ) ) {
            all_parts.push_back( prt );
            if( drop_pos && prt->mount.x() < 0 ) {
                prt->precalc[0].z() = -1;
                prt->precalc[1].z() = -1;
                prt->precalc_z_delta = -1;
            } else if( !drop_pos && prt->mount.x() > 1 ) {
                prt->precalc[0].z() = 1;
                prt->precalc[1].z() = 1;
                prt->precalc_z_delta = 1;
            }
        }
    }
    std::set<int> z_span;
    for( vehicle_part *prt : all_parts ) {
        z_span.insert( veh.abs_part_pos( *prt ).z() );
    }
    REQUIRE( z_span.size() > 1 );

    creature_tracker &creatures = get_creature_tracker();
    Creature *existing_creature =
        creatures.creature_at<Creature>( map_starting_point );
    if( existing_creature ) {
        CAPTURE( existing_creature->as_character() );
        CAPTURE( existing_creature->as_avatar() );
        CAPTURE( existing_creature->is_monster() );
        REQUIRE( !existing_creature );
    }
    monster *dmon_p = g->place_critter_at( pseudo_debug_mon, map_starting_point );
    REQUIRE( dmon_p );
    monster &dmon = *dmon_p;

    for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
        for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
            here.ter_set( tripoint_bub_ms( x, y, 0 ), ter_id( "t_pavement" ) );
        }
    }

    here.vehmove();
    for( vehicle_part *prt : all_parts ) {
        CHECK( veh.abs_part_pos( *prt ).z() == 0 );
    }
    CHECK( dmon.posz() == 0 );
    CHECK( veh.pos_abs().z() == 0 );
}

static void test_leveling( const std::string &type )
{
    SECTION( type + " body drop" ) {
        level_out( vproto_id( type ), true );
    }
    SECTION( type + " edge drop" ) {
        level_out( vproto_id( type ), false );
    }
}

static std::vector<std::string> level_vehs_to_test = {{
        "beetle",
    }
};

TEST_CASE( "vehicle_level_test", "[vehicle][ramp]" )
{
    for( const std::string &veh : level_vehs_to_test ) {
        test_leveling( veh );
    }
}
