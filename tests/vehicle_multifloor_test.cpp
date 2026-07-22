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

TEST_CASE( "upper_deck_mount_requires_vertical_connector", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    const vpart_info &floor = vpart_id( "hdframe" ).obj();

    // A tile far above open ground with no connector beneath must be rejected.
    const tripoint_rel_ms unsupported( 0, 0, 1 );
    CHECK( !veh->can_mount( unsupported, floor ).success() );
}

static const vpart_id vpart_frame( "frame" );

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
