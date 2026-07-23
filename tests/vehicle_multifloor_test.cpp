#include "cata_catch.h"
#include "map.h"
#include "map_helpers.h"
#include "vehicle.h"
#include "veh_type.h"
#include "type_id.h"
#include "coordinates.h"

static const vproto_id vehicle_prototype_car( "car" );

TEST_CASE( "vehicle_prototype_parts_default_to_z_zero", "[vehicle][multifloor]" )
{
    // Every shipped vehicle omits "z", so all authored parts must land on z == 0.
    const vehicle_prototype &proto = vehicle_prototype_car.obj();
    REQUIRE( !proto.parts.empty() );
    for( const vehicle_prototype::part_def &pt : proto.parts ) {
        CHECK( pt.pos.z() == 0 );
    }
}

TEST_CASE( "vehicle_install_part_accepts_tripoint_mount", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // The 3D overload with z == 0 must behave exactly like the 2D one.
    const tripoint_rel_ms mount( 0, 0, 0 );
    CHECK( !veh->parts_at_relative( mount, false, false ).empty() );
}

static const vpart_id vpart_ladder_internal( "ladder_internal" );

TEST_CASE( "vertical_connector_flag_is_recognized", "[vehicle][multifloor]" )
{
    // The flag must resolve through the fast-path enum, not just the string set.
    CHECK( vpart_ladder_internal.obj().has_flag( VPFLAG_VERTICAL_CONNECTOR ) );
}

static const vpart_id vpart_frame( "frame" );

TEST_CASE( "upper_deck_mount_requires_vertical_connector", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    const vpart_info &floor = vpart_id( "hdframe" ).obj();

    // Cheap check: (0, 0, 1) is simply empty, so plain planar adjacency alone
    // already rejects it. This alone would still pass with the connector gate
    // deleted, so it does not prove anything about the gate on its own.
    const tripoint_rel_ms unsupported( 0, 0, 1 );
    CHECK( !veh->can_mount( unsupported, floor ).success() );

    // The discriminating case: mirror upper_deck_mount_allowed_above_connector's
    // setup exactly, but install a plain structural frame (NOT a vertical
    // connector) on the new ground tile. (-1, -2) is a distinct tile from that
    // test's (2, -2), planar-adjacent to the car's real structure at (-1, -1),
    // so the frame installs there. Real structure directly below is present,
    // but with no connector, z=1 above it must still be rejected -- proving
    // it is specifically the connector, not "any part below," that legalizes
    // an upper-deck mount.
    const tripoint_rel_ms ground( -1, -2, 0 );
    REQUIRE( veh->install_part( here, ground, vpart_frame ) >= 0 );

    const tripoint_rel_ms above( -1, -2, 1 );
    CHECK( !veh->can_mount( above, floor ).success() );
}

TEST_CASE( "upper_deck_mount_allowed_above_connector", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // Every occupied tile in the stock "car" already carries a "center"-location
    // part (seat/door/windshield/board/...), which would conflict with
    // ladder_internal's own "center" location.  Extend the vehicle by one tile
    // instead: (2, -2) is just outside the car's footprint but planar-adjacent
    // to the real structural frame at (2, -1), so a plain frame can be mounted
    // there, and the ladder can then be mounted on top of it.
    const tripoint_rel_ms ground( 2, -2, 0 );
    REQUIRE( veh->install_part( here, ground, vpart_frame ) >= 0 );
    REQUIRE( veh->install_part( here, ground, vpart_ladder_internal ) >= 0 );

    const tripoint_rel_ms above( 2, -2, 1 );
    CHECK( veh->can_mount( above, vpart_id( "hdframe" ).obj() ).success() );
}

static const vproto_id vehicle_prototype_test_bus_2floor( "test_bus_2floor" );

TEST_CASE( "two_floor_bus_prototype_has_upper_deck_parts", "[vehicle][multifloor]" )
{
    const vehicle_prototype &proto = vehicle_prototype_test_bus_2floor.obj();
    int upper = 0;
    for( const vehicle_prototype::part_def &pt : proto.parts ) {
        if( pt.pos.z() == 1 ) {
            upper++;
        }
    }
    CHECK( upper > 0 );
}

TEST_CASE( "two_floor_bus_spawns_with_parts_on_both_decks", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    CHECK( !veh->parts_at_relative( tripoint_rel_ms( 0, 0, 0 ), false, false ).empty() );
    CHECK( !veh->parts_at_relative( tripoint_rel_ms( 0, 0, 1 ), false, false ).empty() );
}

// M3 §6.3 composition matrix (mount side): after precalc_mounts, every part's
// precalc.z must equal its mount.z, across all 4 cardinal rotations and both
// decks (with no ramp displacement, precalc_z_delta == 0). This is the NEW logic
// M3 introduces; the ramp side (mount.z=0 x ramp) is covered by vehicle_ramp_test,
// and the cross term (mount.z=1 x ramp) lands with the driving test in M5.
TEST_CASE( "precalc_z_composes_from_mount_z_across_rotations", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // Prove the bus actually has an upper deck, else the test is vacuous.
    bool saw_upper = false;
    for( const vpart_reference &vpr : veh->get_all_parts() ) {
        if( !vpr.part().removed && vpr.part().mount.z() == 1 ) {
            saw_upper = true;
            break;
        }
    }
    REQUIRE( saw_upper );

    for( const units::angle rot : {
                0_degrees, 90_degrees, 180_degrees, 270_degrees
            } ) {
        veh->precalc_mounts( 0, rot, point_rel_ms::zero );
        for( const vpart_reference &vpr : veh->get_all_parts() ) {
            const vehicle_part &vp = vpr.part();
            if( vp.removed ) {
                continue;
            }
            INFO( "rotation " << to_degrees( rot ) << " mount z " << vp.mount.z() );
            CHECK( vp.precalc[0].z() == vp.mount.z() );
        }
    }
}

TEST_CASE( "refresh_bounding_box_tracks_upper_deck_z", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    veh->refresh();
    CHECK( veh->mount_min_z() == 0 );
    CHECK( veh->mount_max_z() == 1 );
}

TEST_CASE( "single_floor_vehicle_bounding_box_z_is_zero", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    veh->refresh();
    // No-op guarantee: every shipped vehicle stays a single ground deck.
    CHECK( veh->mount_min_z() == 0 );
    CHECK( veh->mount_max_z() == 0 );
}

TEST_CASE( "two_floor_bus_upper_deck_can_carry_cargo", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const int cargo = veh->part_with_feature( tripoint_rel_ms( 1, 0, 1 ), "CARGO", false );
    CHECK( cargo >= 0 );
}

// Returns the index of the structure part at the given relative mount, or -1.
static int structure_part_at( const vehicle &veh, const tripoint_rel_ms &mount )
{
    for( const int idx : veh.parts_at_relative( mount, false ) ) {
        if( veh.part( idx ).info().location == "structure" ) {
            return idx;
        }
    }
    return -1;
}

// NOTE on the "public can_unmount fallback" mentioned in the task brief:
// can_unmount() short-circuits to success() immediately for any part whose
// vpart_info::location isn't "structure" (see vehicle.cpp, "non-structure
// parts don't have extra requirements"). ladder_internal's location is
// "center", so `can_unmount(ladder_part, false)` trivially succeeds
// regardless of the 3D BFS -- asserting `!can_unmount(ladder,...).success()`
// fails even on a correct implementation (confirmed empirically: it fails
// the same way both before and after the is_connected/can_unmount rewrite).
// It is also not possible to remove the structure part co-located with the
// connector directly: can_unmount() refuses to remove a structure part while
// any other non-cable part (the connector itself) still occupies the same
// tile ("Remove all other attached parts first"). So the discriminating
// scenario is built one level removed from the connector: a second
// upper-deck tile that reaches the rest of the vehicle ONLY by a planar hop
// through the tile sitting directly above the connector. Removing that
// bridge tile must be refused because doing so would strand the second
// upper-deck tile -- exactly the "would split the vehicle" check that the
// 3D-aware adjacency gather (can_unmount) and BFS (is_connected) exist to
// catch. A 2D-only implementation projects everything to z==0 and instead
// sees a phantom ground-layer neighbour, wrongly allowing the removal.
TEST_CASE( "upper_deck_removal_blocked_when_only_link_is_connector", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // b0: ground bridge tile, planar-adjacent to the car's real structure,
    // carrying the sole vertical connector up to the upper deck.
    const tripoint_rel_ms b0( 2, -2, 0 );
    REQUIRE( veh->install_part( here, b0, vpart_frame ) >= 0 );
    REQUIRE( veh->install_part( here, b0, vpart_ladder_internal ) >= 0 );

    // u0: upper-deck frame directly above the connector. Its only true 3D
    // neighbours are b0 (down, through the connector) and u1 (planar, same z).
    const tripoint_rel_ms u0( 2, -2, 1 );
    REQUIRE( veh->install_part( here, u0, vpart_frame ) >= 0 );

    // u1: a second upper-deck tile reachable from the rest of the vehicle
    // ONLY through u0 -- there is no connector underneath u1.
    const tripoint_rel_ms u1( 3, -2, 1 );
    REQUIRE( veh->install_part( here, u1, vpart_frame ) >= 0 );

    const int idx_u0 = structure_part_at( *veh, u0 );
    REQUIRE( idx_u0 >= 0 );

    CHECK( !veh->can_unmount( veh->part( idx_u0 ), false ).success() );
}

TEST_CASE( "removing_connector_splits_off_upper_deck", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const int ladder = veh->part_with_feature( tripoint_rel_ms( 0, 0, 0 ),
                       "VERTICAL_CONNECTOR", false );
    REQUIRE( ladder >= 0 );

    // Force the split: remove the only vertical link.
    veh->remove_part( veh->part( ladder ) );
    veh->find_and_split_vehicles( here, {} );

    bool still_has_upper = false;
    for( const vpart_reference &vpr : veh->get_all_parts() ) {
        if( vpr.part().mount.z() == 1 && !vpr.part().removed ) {
            still_has_upper = true;
            break;
        }
    }
    CHECK_FALSE( still_has_upper );
}

TEST_CASE( "part_displayed_at_resolves_per_deck", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // Ground deck at (0,0,0) shows a ground structure part; upper deck at
    // (0,0,1) shows the deck floor. They must resolve to DIFFERENT parts.
    const int ground = veh->part_displayed_at( tripoint_rel_ms( 0, 0, 0 ), false, true, false );
    const int upper  = veh->part_displayed_at( tripoint_rel_ms( 0, 0, 1 ), false, true, false );
    REQUIRE( ground >= 0 );
    REQUIRE( upper >= 0 );
    CHECK( ground != upper );
    CHECK( veh->part( upper ).mount.z() == 1 );
}
