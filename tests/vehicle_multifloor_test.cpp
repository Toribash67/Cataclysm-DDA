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
